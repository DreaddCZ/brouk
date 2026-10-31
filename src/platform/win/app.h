#ifndef BROUK_PLATFORM_WIN_APP_H_
#define BROUK_PLATFORM_WIN_APP_H_

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "cef/history_page_handler.h"
#include "cef/history_service.h"
#include "cef/profile_manager_handler.h"
#include "cef/settings_handler.h"
#include "core/closed_tabs.h"
#include "core/launch_args.h"
#include "core/profile_registry.h"
#include "core/session.h"
#include "include/cef_request_context.h"
#include "platform/win/browser_window.h"
#include "platform/win/internal_page_window.h"
#include "platform/win/settings_watcher.h"

namespace brouk::win {

// The Windows app in the browser process: profiles, settings and the open
// windows (the macOS counterpart is BRAppDelegate). Created before
// CefInitialize; every other method runs on the UI thread.
class App final : public BrowserWindowOwner,
                  public ProfileManagerDelegate,
                  public SettingsPageDelegate {
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
  // content.json_viewer (006), for new renderer processes.
  bool JsonViewerEnabled() const;
  // For the history page (013): the view it opens with, Clean up's keep
  // list of a profile, and clearing cookies, site data and the cache.
  std::string HistoryView() const;
  std::vector<std::string> KeepList(const ProfileId& profile);
  void PreviewProfileData(const ProfileId& profile,
                          std::int64_t since,
                          HistoryPageDelegate::DataPreviewDone done);
  void ClearProfileData(const ProfileId& profile,
                        std::int64_t since,
                        bool cookies,
                        bool cache,
                        std::set<std::string> sites,
                        std::function<void(bool cookies_ok)> done);

  // BrowserWindowOwner:
  CefRefPtr<CefRequestContext> RequestContext(
      const ProfileId& profile) override;
  ProfileId DefaultProfile() override;
  TabStrip::GroupLook ProfileLook(const ProfileId& profile) override;
  TabClientDelegate::SearchEngine SearchEngine() override;
  CefRefPtr<CefRequestContext> InternalContext() override;
  std::unique_ptr<CefMessageRouterBrowserSide::Handler> InternalPageHandler(
      const std::string& page,
      BrowserWindow& window,
      TabId tab) override;
  HistoryService* History() override;
  void OnHistoryChanged(const ProfileId& profile) override;
  const std::vector<HistoryMatch>* RecentPages(
      const ProfileId& profile) override;
  std::string SearchSuggestTemplate() override;
  bool HistorySuggestionsEnabled() override;
  void OpenHelp(const std::string& anchor) override;
  void CloseAllWindows() override;
  void OpenWindow(const std::string& url, const ProfileId& profile) override;
  BrowserWindow* OpenPopupWindow(int width, int height) override;
  bool IsOpenWindow(const BrowserWindow* window) const override;
  void OnWindowDestroyed(BrowserWindow* window) override;
  const std::vector<ProfileInfo>& ProfileList() override;
  void OpenProfileManager() override;
  void OpenSettings(const std::string& url) override;
  void OnSessionChanged() override;
  void OnWindowClosing(BrowserWindow& window) override;
  void OnTabClosedToReopen(ClosedTab tab) override;
  void OnWindowClosedToReopen(std::int64_t window_id,
                              std::vector<ClosedTab> tabs) override;
  const std::deque<ClosedEntry>& RecentlyClosed() override;
  void ReopenClosed(std::optional<std::int64_t> entry_id) override;

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
  // Also SettingsPageDelegate's: link routing comes with W3.
  const LinkRoutingConfig* LinkRouting() override;
  std::optional<Error> SetLinkRouting(LinkRoutingConfig config) override;

  // SettingsPageDelegate (the brouk://settings page, 004):
  const ProfileRegistry& Registry() override;
  std::optional<Settings> ProfileSettings(std::string_view id) override;
  std::optional<Error> SetGlobalSetting(std::string_view key,
                                        SettingValue value) override;
  std::optional<Error> SetProfileSetting(std::string_view id,
                                         std::string_view key,
                                         SettingValue value) override;
  void RevealSettingsFolder() override;
  void OpenClearBrowsingData() override;
  AdblockService* Adblock() override;
  std::string Language() override;
  AutofillManager* Autofill() override;
  void SaveAutofillFile(std::string json, TextSaveDone done) override;
  void OpenAutofillFile(TextOpenDone done) override;
  void ExportSettings(FileDone done) override;
  void ImportSettings(FileDone done) override;
  std::vector<std::string> LinkRoutingProblems() override;
  bool IsDefaultBrowser() override;
  void MakeDefaultBrowser(
      std::function<void(std::optional<Error> error)> done) override;

 private:
  // A launch's URLs as tabs of a new window (Chrome), or of `into` (the
  // front restored window, 015), or an empty tab when none is usable, in the
  // profile named by --profile (005).
  void OpenLaunchRequest(const LaunchRequest& request,
                         BrowserWindow* into = nullptr);
  // Session restore (015). Reads the last run's session and marks this run
  // as running (clean_exit: false), so a crash is noticed next time.
  Session StartSession();
  void RestoreSession(const Session& session);
  // The open normal windows, back to front.
  Session CurrentSession() const;
  // The front normal window that is not closing, or null.
  BrowserWindow* FrontWindow() const;
  // A browser of `profile` for the DevTools protocol: one of its tabs, or a
  // hidden helper browser that `release` closes again (013).
  void WithBrowserInProfile(
      const ProfileId& profile,
      std::function<void(CefRefPtr<CefBrowser> browser,
                         std::function<void()> release)> work);
  void SaveSessionFile(const Session& session);
  // Brouk quits: the session to continue with next time is final.
  void SaveSessionAtQuit();
  // Names, colours or the default profile changed: the windows' groups
  // follow.
  void ProfilesChanged();
  // With the last browser window closed, Brouk quits once help, the
  // profile manager and settings have closed too.
  void QuitWhenDone();
  // Brouk's page windows (help, profile manager, settings), for closing
  // them all.
  std::array<std::unique_ptr<InternalPageWindow>*, 3> PageWindows();
  // A settings file changed on disk (004, live reload).
  void SettingsFilesChanged();
  // Applies the global settings Brouk on Windows has so far: theme and
  // the JSON viewer.
  void ApplySettings();
  // The window the settings and profile pages' dialogs belong to.
  HWND DialogOwner() const;

  ProfileRegistry profiles_;
  // One per profile, made when its first tab opens.
  std::map<ProfileId, CefRefPtr<CefRequestContext>> contexts_;
  CefRefPtr<CefRequestContext> internal_context_;
  std::unique_ptr<InternalPageWindow> help_window_;
  std::unique_ptr<InternalPageWindow> profiles_window_;
  std::unique_ptr<InternalPageWindow> settings_window_;
  std::unique_ptr<SettingsWatcher> settings_watcher_;
  // appearance.theme for web pages (Chromium's colour scheme).
  cef_color_variant_t color_variant_ = CEF_COLOR_VARIANT_SYSTEM;
  // The profile manager was opened at startup instead of a window: it
  // closes once a profile is chosen (Chrome's profile picker).
  bool profile_manager_at_startup_ = false;
  // The last browser window closed: Brouk quits once help has closed too.
  bool quitting_ = false;
  // Session restore (015): session.yaml is written 1.5 s after a change.
  // Once the last normal window starts closing (or Exit), the session is
  // frozen as it was then, and saved with clean_exit: true at quit.
  std::filesystem::path session_path_;
  bool session_ready_ = false;
  bool session_save_scheduled_ = false;
  std::optional<Session> frozen_session_;
  // Tabs and windows closed in this run (013).
  ClosedTabList closed_tabs_;
  // History (013): one database per profile, on its own thread; each
  // profile's latest pages for the History menu, read after each visit.
  std::unique_ptr<HistoryService> history_;
  std::map<ProfileId, std::vector<HistoryMatch>> recent_pages_;
  // Hidden helper browsers still open (clearing data); Brouk quits after.
  int helper_browsers_ = 0;
  std::vector<std::unique_ptr<BrowserWindow>> windows_;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_APP_H_
