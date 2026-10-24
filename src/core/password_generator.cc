#include "core/password_generator.h"

#include <algorithm>
#include <string_view>
#include <vector>

#include "core/crypto.h"

namespace brouk {

namespace {

constexpr std::string_view kUpper = "ABCDEFGHJKLMNPQRSTUVWXYZ";
constexpr std::string_view kLower = "abcdefghijkmnpqrstuvwxyz";
constexpr std::string_view kDigits = "23456789";
constexpr std::string_view kSymbols = "-_.:!";

char RandomFrom(std::string_view alphabet) {
  return alphabet[RandomUniform(static_cast<std::uint32_t>(alphabet.size()))];
}

}  // namespace

std::string GeneratePassword(const PasswordRules& rules) {
  std::vector<std::string_view> kinds;
  if (rules.upper) {
    kinds.push_back(kUpper);
  }
  if (rules.lower) {
    kinds.push_back(kLower);
  }
  if (rules.digits) {
    kinds.push_back(kDigits);
  }
  if (rules.symbols) {
    kinds.push_back(kSymbols);
  }
  if (kinds.empty()) {
    kinds = {kLower, kDigits};
  }
  const auto length = static_cast<std::size_t>(std::clamp(rules.length, 8, 64));
  std::string all;
  for (std::string_view kind : kinds) {
    all += kind;
  }
  std::string password;
  // One of each kind, then any, then shuffled (Fisher-Yates).
  for (std::string_view kind : kinds) {
    password.push_back(RandomFrom(kind));
  }
  while (password.size() < length) {
    password.push_back(RandomFrom(all));
  }
  for (std::size_t i = password.size() - 1; i > 0; --i) {
    std::size_t j = RandomUniform(static_cast<std::uint32_t>(i + 1));
    std::swap(password[i], password[j]);
  }
  return password;
}

}  // namespace brouk
