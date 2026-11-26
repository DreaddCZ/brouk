#include "platform/win/store_package.h"

#include <windows.h>

#include <appmodel.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <cwchar>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

#include "brouk/app_info.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

// The installer's AppId (src/platform/win/installer/brouk.iss): Inno Setup
// keeps the uninstall entry under "<AppId>_is1". Must match brouk.iss.
constexpr wchar_t kInstallerUninstallKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
    L"{DE9B09C1-014E-44EE-89A8-AEB2A3DD23D0}_is1";

std::optional<std::wstring> ReadString(HKEY root,
                                       const std::wstring& key,
                                       const wchar_t* value) {
  DWORD size = 0;
  if (RegGetValueW(root, key.c_str(), value, RRF_RT_REG_SZ, nullptr, nullptr,
                   &size) != ERROR_SUCCESS ||
      size < sizeof(wchar_t)) {
    return std::nullopt;
  }
  std::wstring text(size / sizeof(wchar_t), L'\0');
  if (RegGetValueW(root, key.c_str(), value, RRF_RT_REG_SZ, nullptr,
                   text.data(), &size) != ERROR_SUCCESS) {
    return std::nullopt;
  }
  text.resize(wcsnlen(text.c_str(), text.size()));
  return text;
}

void OpenFixedPage(const wchar_t* page, const wchar_t* fallback) {
  // Fixed pages only, opened by the shell (no program runs with arguments
  // from anywhere else).
  const HINSTANCE opened = ShellExecuteW(  // nosemgrep: cpp-process-spawn
      nullptr, L"open", page, nullptr, nullptr, SW_SHOWNORMAL);
  if (reinterpret_cast<INT_PTR>(opened) <= 32 && fallback != nullptr) {
    ShellExecuteW(  // nosemgrep: cpp-process-spawn
        nullptr, L"open", fallback, nullptr, nullptr, SW_SHOWNORMAL);
  }
}

}  // namespace

std::wstring StorePackageFamilyName() {
  UINT32 length = 0;
  if (GetCurrentPackageFamilyName(&length, nullptr) !=
          ERROR_INSUFFICIENT_BUFFER ||
      length == 0) {
    return {};
  }
  std::vector<wchar_t> name(length);
  if (GetCurrentPackageFamilyName(&length, name.data()) != ERROR_SUCCESS) {
    return {};
  }
  return std::wstring(name.data());
}

bool IsStoreApp() {
  UINT32 length = 0;
  // APPMODEL_ERROR_NO_PACKAGE outside a package; a packaged process is
  // asked for the buffer size.
  return GetCurrentPackageFullName(&length, nullptr) ==
         ERROR_INSUFFICIENT_BUFFER;
}

std::wstring StoreAppUserModelId() {
  const std::wstring family = StorePackageFamilyName();
  // The manifest's Application Id is the app name (AppxManifest.xml.in).
  return family.empty() ? std::wstring() : family + L"!" + Utf8ToWide(kAppName);
}

bool IsStoreAppDefaultBrowser() {
  const std::wstring aumid = StoreAppUserModelId();
  if (aumid.empty()) {
    return false;
  }
  Microsoft::WRL::ComPtr<IApplicationAssociationRegistration> registration;
  if (FAILED(CoCreateInstance(
          CLSID_ApplicationAssociationRegistration, nullptr,
          CLSCTX_INPROC_SERVER, IID_IApplicationAssociationRegistration,
          reinterpret_cast<void**>(registration.ReleaseAndGetAddressOf())))) {
    return false;
  }
  for (const wchar_t* scheme : {L"http", L"https"}) {
    LPWSTR prog_id = nullptr;
    std::optional<std::wstring> owner;
    if (SUCCEEDED(registration->QueryCurrentDefault(scheme, AT_URLPROTOCOL,
                                                    AL_EFFECTIVE, &prog_id)) &&
        prog_id != nullptr) {
      owner = ReadString(HKEY_CLASSES_ROOT,
                         std::wstring(prog_id) + L"\\Application",
                         L"AppUserModelID");
    }
    CoTaskMemFree(prog_id);
    if (!owner || _wcsicmp(owner->c_str(), aumid.c_str()) != 0) {
      return false;
    }
  }
  return true;
}

void OpenStoreAppDefaultAppsSettings() {
  const std::wstring aumid = StoreAppUserModelId();
  const std::wstring page = L"ms-settings:defaultapps?registeredAUMID=" + aumid;
  OpenFixedPage(aumid.empty() ? L"ms-settings:defaultapps" : page.c_str(),
                L"ms-settings:defaultapps");
}

void OpenStoreUpdates() {
  OpenFixedPage(L"ms-windows-store://downloadsandupdates", nullptr);
}

bool IsInstallerCopyInstalled() {
  return ReadString(HKEY_CURRENT_USER, kInstallerUninstallKey,
                    L"UninstallString")
      .has_value();
}

void OpenInstalledAppsSettings() {
  OpenFixedPage(L"ms-settings:appsfeatures", nullptr);
}

bool RelaunchStoreApp() {
  if (!IsStoreApp()) {
    return false;
  }
  // brouk.exe itself, without arguments: a process this one starts keeps
  // the package identity (as CEF's own child processes do).
  std::vector<wchar_t> path(MAX_PATH);
  for (;;) {
    const DWORD length = GetModuleFileNameW(nullptr, path.data(),
                                            static_cast<DWORD>(path.size()));
    if (length == 0) {
      return false;
    }
    if (length < path.size()) {
      break;
    }
    path.resize(path.size() * 2);
  }
  std::wstring command = L"\"" + std::wstring(path.data()) + L"\"";
  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process = {};
  if (!CreateProcessW(  // nosemgrep: cpp-process-spawn
          path.data(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
          nullptr, &startup, &process)) {
    return false;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}

}  // namespace brouk::win
