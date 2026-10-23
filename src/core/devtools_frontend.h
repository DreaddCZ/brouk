#ifndef BROUK_CORE_DEVTOOLS_FRONTEND_H_
#define BROUK_CORE_DEVTOOLS_FRONTEND_H_

#include <string_view>

namespace brouk {

// Chromium's DevTools frontend, embedded by Brouk in a tab's dock (006).
// CEF can show the stock DevTools only in a window of its own on macOS, so
// Brouk loads the frontend (shipped in CEF) in a normal browser view and
// connects it to the tab itself, as Chrome's DevToolsUIBindings does.
inline constexpr std::string_view kDevToolsFrontendUrl =
    "devtools://devtools/bundled/devtools_app.html";

// Chromium's script that turns the native DevToolsHost into the frontend's
// InspectorFrontendHost; loaded from the frontend's own resources so it
// always matches the frontend.
inline constexpr std::string_view kDevToolsCompatibilityScriptUrl =
    "devtools://devtools/bundled/devtools_compatibility.js";

// True for pages of the bundled DevTools frontend.
inline bool IsDevToolsFrontendUrl(std::string_view url) {
  return url.starts_with("devtools://devtools/bundled/");
}

// Process message (frontend renderer -> browser). Arguments: kind and
// payload, both strings. Kinds: an embedder message (JSON, as Chrome's
// DevToolsHost.sendMessageToEmbedder) and text to copy.
inline constexpr std::string_view kDevToolsHostMessage = "brouk.devtools";
inline constexpr std::string_view kDevToolsEmbedderKind = "embedder";
inline constexpr std::string_view kDevToolsCopyKind = "copy";

}  // namespace brouk

#endif  // BROUK_CORE_DEVTOOLS_FRONTEND_H_
