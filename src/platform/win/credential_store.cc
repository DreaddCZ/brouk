#include "platform/win/credential_store.h"

#include <windows.h>

#include <wincred.h>

#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "brouk/app_info.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

Error CredentialError(std::string_view what, DWORD code) {
  return Error{std::string(what) + " (Windows error " + std::to_string(code) +
               ")"};
}

class CredentialStore : public SecretStore {
 public:
  explicit CredentialStore(std::wstring prefix) : prefix_(std::move(prefix)) {}

  Result<std::optional<std::string>> Read(std::string_view account) override {
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(Target(account).c_str(), CRED_TYPE_GENERIC, 0,
                   &credential)) {
      const DWORD code = GetLastError();
      if (code == ERROR_NOT_FOUND) {
        return std::optional<std::string>();
      }
      return CredentialError("Reading the key failed", code);
    }
    std::string secret(
        reinterpret_cast<const char*>(credential->CredentialBlob),
        credential->CredentialBlobSize);
    SecureZeroMemory(credential->CredentialBlob,
                     credential->CredentialBlobSize);
    CredFree(credential);
    return std::optional<std::string>(std::move(secret));
  }

  std::optional<Error> Write(std::string_view account,
                             std::string_view secret) override {
    if (secret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
      return Error{"The key is too large for Windows Credential Manager."};
    }
    std::wstring target = Target(account);
    std::wstring user = Utf8ToWide(account);
    // CredWriteW takes a non-const blob: written from a copy, wiped after.
    std::string blob(secret);
    CREDENTIALW credential = {};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = target.data();
    credential.UserName = user.data();
    credential.CredentialBlobSize = static_cast<DWORD>(blob.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(blob.data());
    // On this PC only: the data the keys open is not roaming either.
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    const bool written = CredWriteW(&credential, 0);
    const DWORD code = GetLastError();
    SecureZeroMemory(blob.data(), blob.size());
    if (!written) {
      return CredentialError("Saving the key failed", code);
    }
    return std::nullopt;
  }

  std::optional<Error> Remove(std::string_view account) override {
    if (!CredDeleteW(Target(account).c_str(), CRED_TYPE_GENERIC, 0)) {
      const DWORD code = GetLastError();
      if (code != ERROR_NOT_FOUND) {
        return CredentialError("Removing the key failed", code);
      }
    }
    return std::nullopt;
  }

 private:
  std::wstring Target(std::string_view account) const {
    return prefix_ + L"/" + Utf8ToWide(account);
  }

  const std::wstring prefix_;
};

}  // namespace

std::shared_ptr<SecretStore> CreateCredentialStore(std::wstring prefix) {
  return std::make_shared<CredentialStore>(std::move(prefix));
}

std::wstring CredentialPrefix() {
  std::wstring prefix = Utf8ToWide(kAppName) + L" Keys";
  if (const wchar_t* dev_dir = _wgetenv(L"BROUK_DEV_DATA_DIR")) {
    if (dev_dir[0] != L'\0') {
      prefix += L" (" + std::wstring(dev_dir) + L")";
    }
  }
  return prefix;
}

}  // namespace brouk::win
