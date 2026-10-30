#ifndef BROUK_CEF_COLOR_SCHEME_H_
#define BROUK_CEF_COLOR_SCHEME_H_

#include <string_view>

#include "include/cef_browser.h"

namespace brouk {

// Makes the page's prefers-color-scheme "light" or "dark" (appearance.theme,
// 004), or the system's again for an empty `scheme`. On Windows Chromium
// follows the system's app mode, and a request context's colour scheme does
// not reach Alloy-style browsers, so each browser gets it through DevTools
// emulation, which lasts across its navigations. UI thread.
void EmulateColorScheme(const CefRefPtr<CefBrowser>& browser,
                        std::string_view scheme);

}  // namespace brouk

#endif  // BROUK_CEF_COLOR_SCHEME_H_
