#include "platform/win/user_verification.h"

#include <windows.h>

#include <UserConsentVerifierInterop.h>
#include <roapi.h>
#include <wincred.h>
#include <windows.foundation.h>
#include <windows.security.credentials.ui.h>
#include <wrl/client.h>
#include <wrl/event.h>
#include <wrl/implements.h>
#include <wrl/wrappers/corewrappers.h>

#include <array>
#include <utility>

#include "brouk/app_info.h"
#include "cef/function_task.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

namespace ui = ABI::Windows::Security::Credentials::UI;
using ABI::Windows::Foundation::IAsyncOperation;
using ABI::Windows::Foundation::IAsyncOperationCompletedHandler;
using Microsoft::WRL::ComPtr;
using VerificationOperation =
    IAsyncOperation<ui::UserConsentVerificationResult>;
using VerificationHandler =
    IAsyncOperationCompletedHandler<ui::UserConsentVerificationResult>;

// Wrong passwords before the prompt gives up, as Windows' own prompts.
constexpr int kPasswordAttempts = 3;

// The Windows password of the signed-in user, checked with LogonUser, as
// Chrome does when Windows Hello is not available. Runs the system's
// credential prompt modally: never from inside a CEF callback. UI thread.
bool VerifyWindowsPassword(HWND owner, const std::wstring& reason) {
  const std::wstring caption = Utf8ToWide(kAppName);
  CREDUI_INFOW info = {};
  info.cbSize = sizeof(info);
  info.hwndParent = owner;
  info.pszCaptionText = caption.c_str();
  info.pszMessageText = reason.c_str();
  DWORD auth_error = 0;
  for (int attempt = 0; attempt < kPasswordAttempts; ++attempt) {
    ULONG package = 0;
    LPVOID out = nullptr;
    ULONG out_size = 0;
    BOOL save = FALSE;
    const DWORD answer = CredUIPromptForWindowsCredentialsW(
        &info, auth_error, &package, nullptr, 0, &out, &out_size, &save,
        CREDUIWIN_GENERIC | CREDUIWIN_ENUMERATE_CURRENT_USER);
    if (answer != ERROR_SUCCESS) {
      return false;  // Cancelled, or no prompt.
    }
    std::array<wchar_t, CREDUI_MAX_USERNAME_LENGTH + 1> user = {};
    std::array<wchar_t, CREDUI_MAX_DOMAIN_TARGET_LENGTH + 1> domain = {};
    std::array<wchar_t, CREDUI_MAX_PASSWORD_LENGTH + 1> password = {};
    std::array<wchar_t, CREDUI_MAX_USERNAME_LENGTH + 1> name = {};
    DWORD user_size = static_cast<DWORD>(user.size());
    DWORD domain_size = static_cast<DWORD>(domain.size());
    DWORD password_size = static_cast<DWORD>(password.size());
    const bool unpacked = CredUnPackAuthenticationBufferW(
        0, out, out_size, user.data(), &user_size, domain.data(), &domain_size,
        password.data(), &password_size);
    SecureZeroMemory(out, out_size);
    CoTaskMemFree(out);
    bool verified = false;
    if (unpacked &&
        CredUIParseUserNameW(user.data(), name.data(),
                             static_cast<ULONG>(name.size()), domain.data(),
                             static_cast<ULONG>(domain.size())) == NO_ERROR) {
      HANDLE token = nullptr;
      verified = LogonUserW(name.data(), domain.data(), password.data(),
                            LOGON32_LOGON_NETWORK, LOGON32_PROVIDER_DEFAULT,
                            &token) != FALSE;
      if (token) {
        CloseHandle(token);
      }
    }
    SecureZeroMemory(password.data(), password.size() * sizeof(wchar_t));
    if (verified) {
      return true;
    }
    auth_error = ERROR_LOGON_FAILURE;  // The prompt says it was wrong.
  }
  return false;
}

void VerifyWithPassword(HWND owner,
                        const std::wstring& reason,
                        std::function<void(bool)> done) {
  // A modal prompt: from a task of its own, not inside the caller's.
  CefPostTask(TID_UI, new FunctionTask([owner, reason, done] {
                done(IsWindow(owner) && VerifyWindowsPassword(owner, reason));
              }));
}

// Starts Windows Hello for `owner`. False if it could not start (Windows
// without the API).
bool StartHello(HWND owner,
                const std::wstring& reason,
                std::function<void(bool)> done) {
  ComPtr<IUserConsentVerifierInterop> interop;
  if (FAILED(ABI::Windows::Foundation::GetActivationFactory(
          Microsoft::WRL::Wrappers::HStringReference(
              RuntimeClass_Windows_Security_Credentials_UI_UserConsentVerifier)
              .Get(),
          &interop))) {
    return false;
  }
  ComPtr<VerificationOperation> operation;
  // The interface ID of a parameterized WinRT type exists only through
  // __uuidof (a Microsoft extension, warned about in clang-cl).
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
  const IID operation_iid = __uuidof(VerificationOperation);
#pragma clang diagnostic pop
  if (FAILED(interop->RequestVerificationForWindowAsync(
          owner,
          Microsoft::WRL::Wrappers::HStringReference(reason.c_str()).Get(),
          operation_iid, reinterpret_cast<void**>(operation.GetAddressOf())))) {
    return false;
  }
  // Called on a background thread when the user has answered.
  auto handler = Microsoft::WRL::Callback<Microsoft::WRL::Implements<
      Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
      VerificationHandler, Microsoft::WRL::FtmBase>>(
      [owner, reason, done](VerificationOperation* finished,
                            AsyncStatus status) -> HRESULT {
        ui::UserConsentVerificationResult result =
            ui::UserConsentVerificationResult_Canceled;
        if (status != AsyncStatus::Completed ||
            FAILED(finished->GetResults(&result))) {
          result = ui::UserConsentVerificationResult_Canceled;
        }
        switch (result) {
          case ui::UserConsentVerificationResult_Verified:
            CefPostTask(TID_UI, new FunctionTask([done] { done(true); }));
            break;
          // No Windows Hello on this PC or for this user.
          case ui::UserConsentVerificationResult_DeviceNotPresent:
          case ui::UserConsentVerificationResult_NotConfiguredForUser:
          case ui::UserConsentVerificationResult_DisabledByPolicy:
            CefPostTask(TID_UI, new FunctionTask([owner, reason, done] {
                          VerifyWithPassword(owner, reason, done);
                        }));
            break;
          default:  // Cancelled, busy, too many attempts.
            CefPostTask(TID_UI, new FunctionTask([done] { done(false); }));
            break;
        }
        return S_OK;
      });
  return handler && SUCCEEDED(operation->put_Completed(handler.Get()));
}

}  // namespace

void VerifyUser(HWND owner,
                const std::wstring& reason,
                std::function<void(bool)> done) {
  CEF_REQUIRE_UI_THREAD();
  if (!StartHello(owner, reason, done)) {
    VerifyWithPassword(owner, reason, std::move(done));
  }
}

}  // namespace brouk::win
