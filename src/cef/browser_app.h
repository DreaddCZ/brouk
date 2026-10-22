#ifndef BROUK_CEF_BROWSER_APP_H_
#define BROUK_CEF_BROWSER_APP_H_

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "include/cef_app.h"

namespace brouk {

// CefApp for the browser (main) process. It only forwards process-level events
// to the platform layer, which owns windows and tabs.
class BrowserApp : public CefApp, public CefBrowserProcessHandler {
 public:
  struct Callbacks {
    // UI thread. CEF is ready: open the first window.
    std::function<void()> on_context_initialized;
    // UI thread. The app was started again while running (CEF's process
    // singleton). `urls` are the new launch's arguments (untrusted input,
    // like anything typed into the address bar): Chrome opens them as tabs,
    // or a new window when there are none.
    std::function<void(std::vector<std::string> urls)> on_relaunch;
  };

  explicit BrowserApp(Callbacks callbacks) : callbacks_(std::move(callbacks)) {}
  BrowserApp(const BrowserApp&) = delete;
  BrowserApp& operator=(const BrowserApp&) = delete;

  // CefApp:
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  void OnBeforeCommandLineProcessing(
      const CefString& process_type,
      CefRefPtr<CefCommandLine> command_line) override;

  // CefBrowserProcessHandler:
  void OnRegisterCustomPreferences(
      cef_preferences_type_t type,
      CefRawPtr<CefPreferenceRegistrar> registrar) override;
  void OnContextInitialized() override;
  bool OnAlreadyRunningAppRelaunch(CefRefPtr<CefCommandLine> command_line,
                                   const CefString& current_directory) override;

 private:
  Callbacks callbacks_;

  IMPLEMENT_REFCOUNTING(BrowserApp);
};

}  // namespace brouk

#endif  // BROUK_CEF_BROWSER_APP_H_
