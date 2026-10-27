#ifndef BROUK_CEF_DEVTOOLS_CALL_H_
#define BROUK_CEF_DEVTOOLS_CALL_H_

#include <functional>
#include <string>

#include "include/cef_browser.h"
#include "include/cef_values.h"

namespace brouk {

// Runs one Chrome DevTools Protocol method on `browser` and calls `done`
// once with whether it succeeded and its result (JSON). `done` is called
// later on the UI thread, also when the browser goes away first. UI thread.
using DevToolsCallDone = std::function<void(bool ok, std::string result)>;
void CallDevToolsMethod(const CefRefPtr<CefBrowser>& browser,
                        const std::string& method,
                        const CefRefPtr<CefDictionaryValue>& params,
                        DevToolsCallDone done);

}  // namespace brouk

#endif  // BROUK_CEF_DEVTOOLS_CALL_H_
