#ifndef BROUK_CORE_SECURITY_CHECK_H_
#define BROUK_CORE_SECURITY_CHECK_H_

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/request_log.h"
#include "core/result.h"

namespace brouk {

// What the security check (029) looks at from a tab's recorded requests:
// the page's own load and what it requested since. The checks themselves
// run in the tools page; this only picks the requests.
struct SecurityCheckRequests {
  // The page's main document (the last recorded load of the page's URL),
  // or null when the dev tools were turned on after the page loaded.
  const RecordedRequest* document = nullptr;
  // Fetch and XHR requests of the page's own site (subdomains included)
  // that got a response, oldest first, at most kMaxSiteRequests (newest
  // kept).
  std::vector<const RecordedRequest*> site_requests;
  // Every request since the document, any site, oldest first, at most
  // kMaxPageRequests (newest kept): for mixed content and secrets in URLs.
  std::vector<const RecordedRequest*> page_requests;

  static constexpr std::size_t kMaxSiteRequests = 200;
  static constexpr std::size_t kMaxPageRequests = 1000;
};

SecurityCheckRequests CollectSecurityCheckRequests(const RequestLog& log,
                                                   std::string_view page_url);

// The site the check's findings are hidden for: the registrable domain of
// the page's host ("example.co.uk"); empty for pages that are not http(s).
std::string SecurityCheckSite(std::string_view page_url);

// Findings hidden as "known, accepted", per site; stored per profile in
// profiles/<id>/security-check.yaml. Finding ids come from the tools page
// ("cookie-httponly:session"), so they are checked before use.
using HiddenFindings = std::map<std::string, std::vector<std::string>>;

inline constexpr std::size_t kMaxHiddenPerSite = 500;
inline constexpr std::size_t kMaxHiddenSites = 2000;

// Whether `id` can be a finding id: 1–200 printable ASCII characters
// without spaces.
bool IsFindingId(std::string_view id);

// A missing or unreadable file gives nothing hidden (problems are only
// cosmetic: a hidden finding shows again).
HiddenFindings LoadHiddenFindings(const std::filesystem::path& path);
std::optional<Error> SaveHiddenFindings(const std::filesystem::path& path,
                                        const HiddenFindings& hidden);

}  // namespace brouk

#endif  // BROUK_CORE_SECURITY_CHECK_H_
