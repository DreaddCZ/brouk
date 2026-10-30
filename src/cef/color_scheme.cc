#include "cef/color_scheme.h"

#include <string>

#include "include/cef_values.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

void EmulateColorScheme(const CefRefPtr<CefBrowser>& browser,
                        std::string_view scheme) {
  CEF_REQUIRE_UI_THREAD();
  if (!browser) {
    return;
  }
  CefRefPtr<CefDictionaryValue> feature = CefDictionaryValue::Create();
  feature->SetString("name", "prefers-color-scheme");
  feature->SetString("value", std::string(scheme));
  CefRefPtr<CefListValue> features = CefListValue::Create();
  features->SetDictionary(0, feature);
  CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
  params->SetList("features", features);
  // The result does not matter: a page that is gone needs no scheme.
  browser->GetHost()->ExecuteDevToolsMethod(0, "Emulation.setEmulatedMedia",
                                            params);
}

}  // namespace brouk
