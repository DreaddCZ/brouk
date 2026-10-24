#ifndef BROUK_TESTS_CORE_FAKE_SECRET_STORE_H_
#define BROUK_TESTS_CORE_FAKE_SECRET_STORE_H_

#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "core/secret_store.h"

namespace brouk {

// An in-memory keychain for tests.
class FakeSecretStore : public SecretStore {
 public:
  Result<std::optional<std::string>> Read(std::string_view account) override {
    if (fail) {
      return Error{"locked"};
    }
    auto it = items.find(std::string(account));
    if (it == items.end()) {
      return std::optional<std::string>();
    }
    return std::optional<std::string>(it->second);
  }
  std::optional<Error> Write(std::string_view account,
                             std::string_view secret) override {
    if (fail) {
      return Error{"locked"};
    }
    items[std::string(account)] = std::string(secret);
    return std::nullopt;
  }
  std::optional<Error> Remove(std::string_view account) override {
    items.erase(std::string(account));
    return std::nullopt;
  }

  std::map<std::string, std::string> items;
  bool fail = false;
};

}  // namespace brouk

#endif  // BROUK_TESTS_CORE_FAKE_SECRET_STORE_H_
