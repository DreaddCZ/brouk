#ifndef BROUK_CORE_SECURITY_STATE_H_
#define BROUK_CORE_SECURITY_STATE_H_

#include <string>
#include <string_view>

namespace brouk {

// What the address bar shows about a page's connection (012, item 6).
enum class SecurityLevel {
  kNone,       // Local or internal page (file:, about:, chrome:, data:): "i".
  kSecure,     // Valid HTTPS: lock.
  kNotSecure,  // Plain HTTP or mixed content: "Not secure".
  kDangerous,  // HTTPS with a certificate error the user proceeded past.
};

struct ConnectionFacts {
  std::string_view url;
  bool is_secure_connection = false;  // HTTPS without certificate errors.
  bool has_certificate_error = false;
  bool has_insecure_content = false;  // Mixed content that was loaded.
};

// Chrome's rules: https with a valid certificate is secure unless insecure
// content was loaded; a certificate error the user accepted is dangerous;
// http (except localhost, which is a secure context) is not secure.
SecurityLevel SecurityLevelFor(const ConnectionFacts& facts);

// Popover headline, e.g. "Connection is secure".
std::string SecuritySummary(SecurityLevel level);

}  // namespace brouk

#endif  // BROUK_CORE_SECURITY_STATE_H_
