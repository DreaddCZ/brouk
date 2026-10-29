#ifndef BROUK_PLATFORM_WIN_APP_H_
#define BROUK_PLATFORM_WIN_APP_H_

#include <memory>
#include <string>
#include <vector>

#include "core/launch_args.h"
#include "core/profile_registry.h"
#include "include/cef_request_context.h"
#include "platform/win/browser_window.h"

namespace brouk::win {

// The Windows app in the browser process: profiles, settings and the open
// windows (the macOS counterpart is BRAppDelegate). Created before
// CefInitialize; every other method runs on the UI thread.
class App : public BrowserWindowOwner {
 public:
  App();
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  // CEF is ready: opens the first window with the command line's URLs.
  void OnContextInitialized();
  // Brouk was started again while running (CEF's process singleton).
  void OnRelaunch(const LaunchRequest& request);
  // After the message loop ends, before CefShutdown: drops every CEF object.
  void ReleaseBrowserObjects();

  // BrowserWindowOwner:
  CefRefPtr<CefRequestContext> RequestContext() override;
  TabClientDelegate::SearchEngine SearchEngine() override;
  void OpenWindow(const std::string& url) override;
  void OnWindowDestroyed(BrowserWindow* window) override;

 private:
  // A launch's URLs, each in a window until tabs exist (W1); an empty
  // window when none is usable. --profile waits for profiles (W2).
  void OpenLaunchRequest(const LaunchRequest& request);

  ProfileRegistry profiles_;
  CefRefPtr<CefRequestContext> context_;
  std::vector<std::unique_ptr<BrowserWindow>> windows_;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_APP_H_
