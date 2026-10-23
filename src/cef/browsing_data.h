#ifndef BROUK_CEF_BROWSING_DATA_H_
#define BROUK_CEF_BROWSING_DATA_H_

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_request_context.h"

namespace brouk {

// Clear browsing data (013) for one profile, with what CEF offers: cookies
// by creation time, site data per origin through the DevTools protocol, and
// the whole HTTP cache (CEF cannot clear the cache by time). UI thread.

// Cookies created at or after `since` (seconds since 1970; 0 for all):
// their number and the sites (hosts, without a leading dot) they belong to.
// With `remove`, they are deleted. The count is -1 if the cookie store did
// not answer within 10 seconds.
using CookiesDone = std::function<void(int count, std::set<std::string> hosts)>;
void VisitCookiesSince(const CefRefPtr<CefRequestContext>& context,
                       std::int64_t since,
                       bool remove,
                       CookiesDone done);

// Clears local storage, IndexedDB, Cache Storage, service workers, file
// systems and similar data of `origins` ("https://example.com") in the
// browser's profile, through `browser`'s DevTools protocol.
void ClearSiteData(const CefRefPtr<CefBrowser>& browser,
                   const std::vector<std::string>& origins,
                   std::function<void()> done);

// The origins whose site data goes with `hosts`: http and https of each.
std::vector<std::string> OriginsOfHosts(const std::set<std::string>& hosts);

void ClearHttpCache(const CefRefPtr<CefRequestContext>& context,
                    std::function<void()> done);

// A client for a hidden helper browser (about:blank in a profile, for the
// DevTools protocol when the profile has no tab): `created` gets the
// browser, `closed` runs when it is gone.
CefRefPtr<CefClient> MakeHelperBrowserClient(
    std::function<void(CefRefPtr<CefBrowser>)> created,
    std::function<void()> closed);

}  // namespace brouk

#endif  // BROUK_CEF_BROWSING_DATA_H_
