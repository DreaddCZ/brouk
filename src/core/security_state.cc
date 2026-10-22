#include "core/security_state.h"

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

namespace brouk {

namespace {

std::string LowerScheme(std::string_view url) {
  std::size_t colon = url.find(':');
  if (colon == std::string_view::npos) {
    return {};
  }
  std::string scheme(url.substr(0, colon));
  for (char& c : scheme) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return scheme;
}

// "http://localhost:3000/x" -> true. Chrome treats localhost as a secure
// context and does not warn about it.
bool IsLocalhost(std::string_view url) {
  std::size_t start = url.find("://");
  if (start == std::string_view::npos) {
    return false;
  }
  std::string_view rest = url.substr(start + 3);
  std::size_t end = rest.find_first_of(":/?#");
  std::string_view host = rest.substr(0, end);
  return host == "localhost" || host == "127.0.0.1" || host == "[::1]" ||
         host.ends_with(".localhost");
}

}  // namespace

SecurityLevel SecurityLevelFor(const ConnectionFacts& facts) {
  std::string scheme = LowerScheme(facts.url);
  if (scheme == "https" || scheme == "wss") {
    if (facts.has_certificate_error) {
      return SecurityLevel::kDangerous;
    }
    if (facts.is_secure_connection && !facts.has_insecure_content) {
      return SecurityLevel::kSecure;
    }
    return SecurityLevel::kNotSecure;
  }
  if (scheme == "http" || scheme == "ws") {
    return IsLocalhost(facts.url) ? SecurityLevel::kNone
                                  : SecurityLevel::kNotSecure;
  }
  return SecurityLevel::kNone;
}

std::string SecuritySummary(SecurityLevel level) {
  switch (level) {
    case SecurityLevel::kSecure:
      return "Connection is secure";
    case SecurityLevel::kNotSecure:
      return "Connection is not secure";
    case SecurityLevel::kDangerous:
      return "Your connection to this site is not private";
    case SecurityLevel::kNone:
      return "Local or internal page";
  }
  return {};
}

}  // namespace brouk
