#ifndef BROUK_CORE_PASSWORD_CHECKUP_H_
#define BROUK_CORE_PASSWORD_CHECKUP_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace brouk {

// The password checkup (008). Weak passwords are rated by the passwords
// page (zxcvbn); reused and breached ones here.

// The breach check against Have I Been Pwned's password list, with
// k-anonymity: only the first 5 hex digits of the password's SHA-1 go to
// the service (GET https://api.pwnedpasswords.com/range/<prefix>); it
// answers with every hash suffix under that prefix, compared here.
inline constexpr std::string_view kPwnedPasswordsUrl =
    "https://api.pwnedpasswords.com/range/";
struct PwnedQuery {
  std::string prefix;  // 5 upper-case hex digits; the only part sent.
  std::string suffix;  // The other 35, kept locally.
};
PwnedQuery MakePwnedQuery(std::string_view password);
// How often the password appears in breaches, from the service's answer
// ("SUFFIX:COUNT" lines; padding lines have a count of 0).
std::int64_t PwnedCount(std::string_view response, std::string_view suffix);

// Groups of passwords used more than once, as indices into `passwords`
// (which may come from several profiles). Each group has at least two
// entries; groups are in order of their first entry.
std::vector<std::vector<std::size_t>> FindReusedPasswords(
    std::span<const std::string> passwords);

}  // namespace brouk

#endif  // BROUK_CORE_PASSWORD_CHECKUP_H_
