#ifndef BROUK_CORE_SECURE_DNS_H_
#define BROUK_CORE_SECURE_DNS_H_

#include <optional>
#include <string>
#include <string_view>

namespace brouk {

// Secure DNS (007): the DNS-over-HTTPS address for the setting dns.secure
// ("cloudflare", …, or "custom" with dns.custom_template). nullopt for
// "off", and for a custom address that is not an https URL (then the system
// DNS stays in use).
std::optional<std::string> SecureDnsTemplate(std::string_view choice,
                                             std::string_view custom);

}  // namespace brouk

#endif  // BROUK_CORE_SECURE_DNS_H_
