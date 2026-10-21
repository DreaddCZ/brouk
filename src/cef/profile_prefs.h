#ifndef BROUK_CEF_PROFILE_PREFS_H_
#define BROUK_CEF_PROFILE_PREFS_H_

#include <string>

#include "include/cef_preference.h"
#include "include/cef_request_context.h"

namespace brouk {

// Small pieces of state stored as Chromium preferences of a profile's request
// context, so they are per profile and saved with the profile's data.
// UI thread only.

// Registers our preferences; call from
// CefBrowserProcessHandler::OnRegisterCustomPreferences.
void RegisterProfilePreferences(cef_preferences_type_t type,
                                CefRawPtr<CefPreferenceRegistrar> registrar);

// "Always allow <origin> to open <scheme> links" (012, item 14); `key` comes
// from ExternalProtocolAllowKey().
bool IsExternalProtocolAllowed(const CefRefPtr<CefRequestContext>& context,
                               const std::string& key);
void AllowExternalProtocol(const CefRefPtr<CefRequestContext>& context,
                           const std::string& key);

}  // namespace brouk

#endif  // BROUK_CEF_PROFILE_PREFS_H_
