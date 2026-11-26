#ifndef BROUK_PLATFORM_WIN_CREDENTIAL_STORE_H_
#define BROUK_PLATFORM_WIN_CREDENTIAL_STORE_H_

#include <memory>
#include <string>

#include "core/secret_store.h"

namespace brouk::win {

// Windows Credential Manager as the core's SecretStore (008, 009, 024):
// generic credentials of the signed-in user, kept on this PC only (not
// roaming), named "<prefix>/<account>". Company policies govern Credential
// Manager, which is why secrets go there (user's decision, 008).
// Thread-safe (the Cred* functions are).
std::shared_ptr<SecretStore> CreateCredentialStore(std::wstring prefix);

// The name prefix of this app's keys: "Brouk Keys", in the Store app (032)
// "Brouk Keys (Microsoft Store)", as each copy has its own data folder and
// deletes its own keys. A developer test run with its own data folder
// (BROUK_DEV_DATA_DIR) gets its own prefix, so it never reads or deletes
// the user's keys.
std::wstring CredentialPrefix();

// The direct download's prefix, also in the Store app (to import its keys).
std::wstring DirectDownloadCredentialPrefix();

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_CREDENTIAL_STORE_H_
