#ifndef BROUK_CORE_SECRET_STORE_H_
#define BROUK_CORE_SECRET_STORE_H_

#include <optional>
#include <string>
#include <string_view>

#include "core/crypto.h"
#include "core/result.h"

namespace brouk {

// The OS keychain, where secrets go (CLAUDE.md): on macOS the login
// keychain, readable only by Brouk's code signature. Implemented by the
// platform; tests use an in-memory one.
class SecretStore {
 public:
  virtual ~SecretStore() = default;

  // The secret saved under `account`; nullopt if there is none; an error if
  // the keychain could not be read (locked, access denied).
  virtual Result<std::optional<std::string>> Read(std::string_view account) = 0;
  virtual std::optional<Error> Write(std::string_view account,
                                     std::string_view secret) = 0;
  virtual std::optional<Error> Remove(std::string_view account) = 0;
};

// Keychain accounts of the encryption keys: one per profile for passwords
// (008), one for the app's autofill data (009).
std::string PasswordKeyAccount(std::string_view profile_id);
inline constexpr std::string_view kAutofillKeyAccount = "autofill-key";

// The key saved under `account`, or a new one saved there first. Fails if
// the keychain cannot be read or written, or holds something that is not a
// key: the data it protects cannot be opened then.
Result<SecretKey> LoadOrCreateKey(SecretStore& store, std::string_view account);

}  // namespace brouk

#endif  // BROUK_CORE_SECRET_STORE_H_
