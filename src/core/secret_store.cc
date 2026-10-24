#include "core/secret_store.h"

namespace brouk {

std::string PasswordKeyAccount(std::string_view profile_id) {
  return "passwords-key-" + std::string(profile_id);
}

Result<SecretKey> LoadOrCreateKey(SecretStore& store,
                                  std::string_view account) {
  Result<std::optional<std::string>> saved = store.Read(account);
  if (!saved) {
    return saved.error();
  }
  if (std::optional<std::string>& text = saved.value(); text.has_value()) {
    std::optional<std::string> bytes = Base64Decode(*text);
    Wipe(*text);
    std::optional<SecretKey> key =
        bytes ? SecretKey::FromBytes(*bytes) : std::nullopt;
    if (bytes) {
      Wipe(*bytes);
    }
    if (!key) {
      return Error{"The keychain item holds no valid key."};
    }
    return *key;
  }
  SecretKey key = SecretKey::Generate();
  std::string encoded = Base64Encode(key.bytes());
  std::optional<Error> error = store.Write(account, encoded);
  Wipe(encoded);
  if (error) {
    return *error;
  }
  return key;
}

}  // namespace brouk
