#ifndef BROUK_CORE_INTERNAL_PAGES_H_
#define BROUK_CORE_INTERNAL_PAGES_H_

#include <optional>
#include <string>
#include <string_view>

namespace brouk {

// Internal pages (specs/001): HTML/TypeScript pages built from web/ into the
// app bundle and shown at brouk://<page>/, e.g. brouk://profiles/ (005).
inline constexpr std::string_view kInternalScheme = "brouk";

// True for brouk://<known page>/... URLs.
bool IsInternalPageUrl(std::string_view url);
// The page of an internal URL ("profiles"), or nullopt.
std::optional<std::string> InternalPageOf(std::string_view url);

// The file to serve for an internal URL, relative to the bundle's web
// folder: brouk://profiles/ -> "profiles/index.html",
// brouk://profiles/app.js -> "profiles/app.js", and the shared modules every
// page imports, brouk://profiles/shared/bridge.js -> "shared/bridge.js".
// Only plain file names (letters, digits, '_', '-', '.') with a known
// extension are served; nullopt for anything else (no "..", no hidden
// files, no query-dependent paths).
std::optional<std::string> InternalResourcePath(std::string_view url);

// MIME type for a served file, by extension.
std::string_view InternalResourceMimeType(std::string_view path);

// Content-Security-Policy of every internal page: only the page's own
// scripts and styles, nothing from the web.
inline constexpr std::string_view kInternalPageCsp =
    "default-src 'self'; script-src 'self'; style-src 'self'; "
    "img-src 'self' data:; object-src 'none'; base-uri 'none'; "
    "form-action 'none'; frame-ancestors 'none'";

}  // namespace brouk

#endif  // BROUK_CORE_INTERNAL_PAGES_H_
