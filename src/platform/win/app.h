#ifndef BROUK_PLATFORM_WIN_APP_H_
#define BROUK_PLATFORM_WIN_APP_H_

#include <memory>
#include <string>
#include <vector>

#include "core/launch_args.h"
#include "core/profile_registry.h"
#include "include/cef_request_context.h"
#include "platform/win/browser_window.h"
#include "platform/win/internal_page_window.h"

namespace brouk::win {

// The Windows app in the browser process: profiles, settings and the open
// windows (the macOS counterpart is BRAppDelegate). Created before
// CefInitialize; every other method runs on the UI thread.
class App final : public BrowserWindowOwner {
 public:
  App();
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  // CEF is ready: opens the first window with the command line's URLs.
  void OnContextInitialized();
  // A web link from help or the licences page (016, 023): a tab in the most
  // recent window, or a new window.
  void OpenWebLink(const std::string& url);
  // Brouk was started again while running (CEF's process singleton).
  void OnRelaunch(const LaunchRequest& request);
  // After the message loop ends, before CefShutdown: drops every CEF object.
  void ReleaseBrowserObjects();

  // BrowserWindowOwner:
  CefRefPtr<CefRequestContext> RequestContext() override;
  TabClientDelegate::SearchEngine SearchEngine() override;
  CefRefPtr<CefRequestContext> InternalContext() override;
  std::unique_ptr<CefMessageRouterBrowserSide::Handler> InternalPageHandler(
      const std::string& page) override;
  void OpenHelp(const std::string& anchor) override;
  void CloseAllWindows() override;
  void OpenWindow(const std::string& url) override;
  BrowserWindow* OpenPopupWindow(int width, int height) override;
  bool IsOpenWindow(const BrowserWindow* window) const override;
  void OnWindowDestroyed(BrowserWindow* window) override;

 private:
  // A launch's URLs as tabs of a new window (Chrome), or an empty tab when
  // none is usable. --profile waits for profiles (W2).
  void OpenLaunchRequest(const LaunchRequest& request);

  ProfileRegistry profiles_;
  CefRefPtr<CefRequestContext> context_;
  CefRefPtr<CefRequestContext> internal_context_;
  std::unique_ptr<InternalPageWindow> help_window_;
  // The last browser window closed: Brouk quits once help has closed too.
  bool quitting_ = false;
  std::vector<std::unique_ptr<BrowserWindow>> windows_;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_APP_H_
