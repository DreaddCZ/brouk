#ifndef BROUK_CEF_BROWSER_APP_H_
#define BROUK_CEF_BROWSER_APP_H_

#include <functional>
#include <utility>

#include "core/launch_args.h"
#include "include/cef_app.h"
#include "include/cef_command_line.h"

namespace brouk {

// CefApp for the browser (main) process. It only forwards process-level events
// to the platform layer, which owns windows and tabs.
class BrowserApp : public CefApp, public CefBrowserProcessHandler {
 public:
  struct Callbacks {
    // UI thread. CEF is ready: open the first window.
    std::function<void()> on_context_initialized;
    // UI thread. The app was started again while running (CEF's process
    // singleton). The request's URLs are untrusted input, like anything typed
    // into the address bar: Chrome opens them as tabs, or a new window when
    // there are none.
    std::function<void(LaunchRequest request)> on_relaunch;
    // UI thread. Current settings passed to new renderer processes.
    std::function<bool()> json_viewer_enabled;
  };

  // What a command line asks for: URLs and --profile (005).
  static LaunchRequest LaunchRequestFrom(
      const CefRefPtr<CefCommandLine>& command_line);

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
  void OnRegisterCustomSchemes(
      CefRawPtr<CefSchemeRegistrar> registrar) override;
  void OnBeforeChildProcessLaunch(
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
