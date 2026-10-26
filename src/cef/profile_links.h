#ifndef BROUK_CEF_PROFILE_LINKS_H_
#define BROUK_CEF_PROFILE_LINKS_H_

#include <functional>
#include <optional>

#include "core/link_routing_rules.h"
#include "core/profile_id.h"
#include "core/result.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_message_router.h"

namespace brouk {

// "Links that open in this profile" (010), saved from the profile manager
// and from the settings page alike: `patterns` (the page's untrusted list)
// become `profile`'s links (SetProfilePatterns). Nothing is saved while a
// pattern is not valid; the answer lists the problems by position
// ({"problems": [{"index", "message"}]}). UI thread.
void SaveProfileLinks(
    const CefRefPtr<CefListValue>& patterns,
    const ProfileId& profile,
    const LinkRoutingConfig& current,
    const std::function<std::optional<Error>(LinkRoutingConfig)>& save,
    const CefRefPtr<CefMessageRouterBrowserSide::Callback>& callback);

}  // namespace brouk

#endif  // BROUK_CEF_PROFILE_LINKS_H_
