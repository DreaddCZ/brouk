#ifndef BROUK_PLATFORM_WIN_DEFAULT_BROWSER_H_
#define BROUK_PLATFORM_WIN_DEFAULT_BROWSER_H_

#include <optional>

#include "core/result.h"

namespace brouk::win {

// Brouk as the default browser on Windows (010), as Chrome does it for a
// per-user install: Brouk registers itself for the signed-in user (HKCU)
// as a web browser with http, https and HTML files, and Windows' Settings
// make it the default (Windows lets no app do that by itself). Links then
// start brouk.exe with "--single-argument <url>", which hands them to the
// running Brouk (CEF's process singleton), where link routing takes them.

// Writes the registration for this brouk.exe (its current path). UI
// thread or any.
std::optional<Error> RegisterAsBrowser();
// At start: a registration made earlier follows brouk.exe when its folder
// moved (a portable copy). Nothing when Brouk was never registered.
void RefreshBrowserRegistration();
// Whether http links open in Brouk now.
bool IsDefaultBrowser();
// Opens Windows' Settings at Brouk's default apps page (Windows 11), or at
// the default apps (Windows 10).
void OpenDefaultAppsSettings();

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_DEFAULT_BROWSER_H_
