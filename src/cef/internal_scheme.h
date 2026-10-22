#ifndef BROUK_CEF_INTERNAL_SCHEME_H_
#define BROUK_CEF_INTERNAL_SCHEME_H_

#include <string>

#include "core/internal_pages.h"
#include "include/cef_request_context.h"
#include "include/cef_scheme.h"

namespace brouk {

// Registers brouk:// as a standard, secure scheme. Must be called from
// CefApp::OnRegisterCustomSchemes in every process (browser and helpers).
inline void AddInternalScheme(CefRawPtr<CefSchemeRegistrar> registrar) {
  registrar->AddCustomScheme(
      std::string(kInternalScheme),
      CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE |
          CEF_SCHEME_OPTION_CORS_ENABLED | CEF_SCHEME_OPTION_FETCH_ENABLED);
}

// Serves internal pages from `web_dir` (the bundle's built web/ folder) in
// `context`. Only that request context can load brouk:// URLs; profiles'
// contexts cannot. UI thread.
void InstallInternalPages(const CefRefPtr<CefRequestContext>& context,
                          const std::string& web_dir);

}  // namespace brouk

#endif  // BROUK_CEF_INTERNAL_SCHEME_H_
