#ifndef BROUK_CEF_AUTOFILL_SETTINGS_H_
#define BROUK_CEF_AUTOFILL_SETTINGS_H_

#include <string>

#include "include/cef_values.h"
#include "include/wrapper/cef_message_router.h"

namespace brouk {

class SettingsPageDelegate;

// The settings page's autofill messages (009, Settings → Autofill): the
// entries and custom fields, editing, deleting with undo, duplicates and
// merging, JSON export and import. `type` starts with "autofill.";
// `message` is the page's (untrusted) request. Returns false for an
// unknown type. UI thread.
bool HandleAutofillSettingsMessage(
    const std::string& type,
    const CefRefPtr<CefDictionaryValue>& message,
    SettingsPageDelegate& delegate,
    const CefRefPtr<CefMessageRouterBrowserSide::Callback>& callback);

}  // namespace brouk

#endif  // BROUK_CEF_AUTOFILL_SETTINGS_H_
