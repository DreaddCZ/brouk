#ifndef BROUK_CEF_SITE_PERMISSIONS_SETTINGS_H_
#define BROUK_CEF_SITE_PERMISSIONS_SETTINGS_H_

#include <string>

#include "include/cef_values.h"
#include "include/wrapper/cef_message_router.h"

namespace brouk {

class SettingsPageDelegate;

// The settings page's site permissions (012): the sites a profile has
// allowed or blocked something for, changed or reset there as in the
// address bar's site info. `type` starts with "permissions."; `message` is
// the page's (untrusted) request. Returns false for an unknown type. UI
// thread.
bool HandleSitePermissionsSettingsMessage(
    const std::string& type,
    const CefRefPtr<CefDictionaryValue>& message,
    SettingsPageDelegate& delegate,
    const CefRefPtr<CefMessageRouterBrowserSide::Callback>& callback);

}  // namespace brouk

#endif  // BROUK_CEF_SITE_PERMISSIONS_SETTINGS_H_
