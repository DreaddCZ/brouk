#ifndef BROUK_CEF_ADBLOCK_SETTINGS_H_
#define BROUK_CEF_ADBLOCK_SETTINGS_H_

#include <string>

#include "cef/adblock_service.h"
#include "core/settings.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_message_router.h"

namespace brouk {

class SettingsPageDelegate;

// The settings page's ad blocking messages (007): filter lists (on/off,
// added by URL, removed, updated) and My filters. `type` starts with
// "adblock."; `message` is the page's (untrusted) request. Returns false
// for an unknown type. UI thread.
bool HandleAdblockSettingsMessage(
    const std::string& type,
    const CefRefPtr<CefDictionaryValue>& message,
    SettingsPageDelegate& delegate,
    const CefRefPtr<CefMessageRouterBrowserSide::Callback>& callback);

}  // namespace brouk

#endif  // BROUK_CEF_ADBLOCK_SETTINGS_H_
