#ifndef BROUK_CEF_LINK_ROUTING_SETTINGS_H_
#define BROUK_CEF_LINK_ROUTING_SETTINGS_H_

#include <string>

#include "include/cef_values.h"
#include "include/wrapper/cef_message_router.h"

namespace brouk {

class SettingsPageDelegate;

// The settings page's link routing messages (010): the routing rules, the
// user's link wrappers (with the built-in ones to show), and making Brouk
// the default browser. `type` starts with "links."; `message` is the
// page's (untrusted) request. Returns false for an unknown type. UI thread.
bool HandleLinkRoutingSettingsMessage(
    const std::string& type,
    const CefRefPtr<CefDictionaryValue>& message,
    SettingsPageDelegate& delegate,
    const CefRefPtr<CefMessageRouterBrowserSide::Callback>& callback);

}  // namespace brouk

#endif  // BROUK_CEF_LINK_ROUTING_SETTINGS_H_
