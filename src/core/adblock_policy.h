#ifndef BROUK_CORE_ADBLOCK_POLICY_H_
#define BROUK_CORE_ADBLOCK_POLICY_H_

#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/adblock_engine.h"
#include "core/adblock_lists.h"
#include "core/url_pattern.h"

namespace brouk {

// How a profile blocks (007): on or off, its categories, and the sites
// where blocking is off.
struct ProfileAdblockPolicy {
  bool enabled = true;
  std::set<FilterCategory> categories = DefaultFilterCategories();
  std::vector<UrlPattern> allowlist = {};
};

// Hosts never blocked by broad list rules (007): localhost, private network
// addresses, .local names, and `extra` (hosts of sites in routing rules).
bool IsProtectedHost(std::string_view host,
                     std::span<const std::string> extra = {});

// The host of a URL, lower case ("" for URLs without one).
std::string HostOfUrl(std::string_view url);
// Whether two hosts belong to different sites (registrable domains,
// approximated by the last two labels, or three for two-letter country
// second-level domains such as co.uk).
bool IsThirdParty(std::string_view request_host, std::string_view page_host);

// What happens to a request after the engine's verdict and the profile's
// rules. A protected host is blocked only by a rule that names it.
struct AdblockDecision {
  bool block = false;
  // The engine matched but the request goes through: an exception, or a
  // protected host.
  bool excepted = false;
  std::string redirect;       // data: URL to serve instead (when blocking).
  std::string rewritten_url;  // Parameters removed (when not blocking).
};
AdblockDecision DecideRequest(const AdblockMatch& match,
                              std::string_view request_host,
                              std::span<const std::string> protected_extra);

// The body of a data: URL ("data:text/javascript;base64,…") and its MIME
// type; nullopt if it is not one.
struct DataUrl {
  std::string mime_type;
  std::string body;
};
std::optional<DataUrl> ParseDataUrl(std::string_view url);

}  // namespace brouk

#endif  // BROUK_CORE_ADBLOCK_POLICY_H_
