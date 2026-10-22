#ifndef BROUK_CEF_SITE_INFO_H_
#define BROUK_CEF_SITE_INFO_H_

#include <cstdint>
#include <string>
#include <vector>

#include "core/permissions.h"
#include "core/security_state.h"
#include "include/cef_browser.h"

namespace brouk {

// What the address bar's site info shows for the visible page (012, items 3
// and 6). UI thread only.
struct SiteInfo {
  std::string origin;  // "https://example.com"; empty for internal pages.
  SecurityLevel level = SecurityLevel::kNone;
  // DER-encoded certificates, leaf first; empty without HTTPS.
  std::vector<std::vector<std::uint8_t>> certificate_chain;
};

SiteInfo GetSiteInfo(const CefRefPtr<CefBrowser>& browser);

// A site permission as the user can set it.
enum class PermissionSetting { kAsk, kAllow, kBlock };

struct SitePermission {
  PermissionKind kind;
  PermissionSetting setting;
};

// Permissions the site has a stored decision for (not "Ask"), in the
// browser's profile.
std::vector<SitePermission> GetSitePermissions(
    const CefRefPtr<CefBrowser>& browser,
    const std::string& origin);

void SetSitePermission(const CefRefPtr<CefBrowser>& browser,
                       const std::string& origin,
                       PermissionKind kind,
                       PermissionSetting setting);

// Back to "Ask" for every permission of the site.
void ResetSitePermissions(const CefRefPtr<CefBrowser>& browser,
                          const std::string& origin);

}  // namespace brouk

#endif  // BROUK_CEF_SITE_INFO_H_
