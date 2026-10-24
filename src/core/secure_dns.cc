#include "core/secure_dns.h"

#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace brouk {

std::optional<std::string> SecureDnsTemplate(std::string_view choice,
                                             std::string_view custom) {
  if (choice == "cloudflare") {
    return "https://cloudflare-dns.com/dns-query";
  }
  if (choice == "quad9") {
    return "https://dns.quad9.net/dns-query";
  }
  if (choice == "adguard") {
    return "https://dns.adguard-dns.com/dns-query";
  }
  if (choice == "nextdns") {
    return "https://dns.nextdns.io";
  }
  if (choice == "controld") {
    return "https://freedns.controld.com/p0";
  }
  if (choice != "custom") {
    return std::nullopt;
  }
  while (!custom.empty() &&
         std::isspace(static_cast<unsigned char>(custom.front())) != 0) {
    custom.remove_prefix(1);
  }
  while (!custom.empty() &&
         std::isspace(static_cast<unsigned char>(custom.back())) != 0) {
    custom.remove_suffix(1);
  }
  constexpr std::string_view kHttps = "https://";
  if (custom.size() <= kHttps.size() || !custom.starts_with(kHttps) ||
      custom.find_first_of(" \t\r\n") != std::string_view::npos) {
    return std::nullopt;
  }
  return std::string(custom);
}

}  // namespace brouk
