#ifndef BROUK_PLATFORM_WIN_APP_H_
#define BROUK_PLATFORM_WIN_APP_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cef/profile_manager_handler.h"
#include "core/launch_args.h"
#include "core/profile_registry.h"
#include "include/cef_request_context.h"
#include "platform/win/browser_window.h"
#include "platform/win/internal_page_window.h"

namespace brouk::win {

// The Windows app in the browser process: profiles, settings and the open
// windows (the macOS counterpart is BRAppDelegate). Created before
// CefInitialize; every other method runs on the UI thread.
class App final : public BrowserWindowOwner, public ProfileManagerDelegate {
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
  CefRefPtr<CefRequestContext> RequestContext(
      const ProfileId& profile) override;
  ProfileId DefaultProfile() override;
  TabStrip::GroupLook ProfileLook(const ProfileId& profile) override;
  TabClientDelegate::SearchEngine SearchEngine() override;
  CefRefPtr<CefRequestContext> InternalContext() override;
  std::unique_ptr<CefMessageRouterBrowserSide::Handler> InternalPageHandler(
      const std::string& page) override;
  void OpenHelp(const std::string& anchor) override;
  void CloseAllWindows() override;
  void OpenWindow(const std::string& url, const ProfileId& profile) override;
  BrowserWindow* OpenPopupWindow(int width, int height) override;
  bool IsOpenWindow(const BrowserWindow* window) const override;
  void OnWindowDestroyed(BrowserWindow* window) override;
  const std::vector<ProfileInfo>& ProfileList() override;
  void OpenProfileManager() override;

  // ProfileManagerDelegate (the brouk://profiles page, 005):
  std::vector<Summary> Profiles() override;
  bool ShowAtStartup() override;
  Result<ProfileId> CreateProfile(const std::string& name,
                                  const std::string& color) override;
  std::optional<Error> UpdateProfile(const ProfileId& id,
                                     const std::string& name,
                                     const std::string& color) override;
  std::optional<Error> SetDefaultProfile(const ProfileId& id) override;
  std::optional<Error> SetShowAtStartup(bool show) override;
  std::optional<Error> OpenProfile(const ProfileId& id) override;
  void DeleteProfile(const ProfileId& id, DeleteDone done) override;
  void ExportProfile(const ProfileId& id, ExportDone done) override;
  void ImportProfile(ImportDone done) override;
  const LinkRoutingConfig* LinkRouting() override;
  std::optional<Error> SetLinkRouting(LinkRoutingConfig config) override;

 private:
  // A launch's URLs as tabs of a new window (Chrome), or an empty tab when
  // none is usable, in the profile named by --profile (005).
  void OpenLaunchRequest(const LaunchRequest& request);
  // Names, colours or the default profile changed: the windows' groups
  // follow.
  void ProfilesChanged();
  // With the last browser window closed, Brouk quits once help and the
  // profile manager have closed too.
  void QuitWhenDone();

  ProfileRegistry profiles_;
  // One per profile, made when its first tab opens.
  std::map<ProfileId, CefRefPtr<CefRequestContext>> contexts_;
  CefRefPtr<CefRequestContext> internal_context_;
  std::unique_ptr<InternalPageWindow> help_window_;
  std::unique_ptr<InternalPageWindow> profiles_window_;
  // The profile manager was opened at startup instead of a window: it
  // closes once a profile is chosen (Chrome's profile picker).
  bool profile_manager_at_startup_ = false;
  // The last browser window closed: Brouk quits once help has closed too.
  bool quitting_ = false;
  std::vector<std::unique_ptr<BrowserWindow>> windows_;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_APP_H_
