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

#include "cef/adblock_service.h"
#include "cef/autofill_manager.h"
#include "cef/download_service.h"
#include "cef/history_page_handler.h"
#include "cef/history_service.h"
#include "cef/password_manager.h"
#include "cef/profile_manager_handler.h"
#include "cef/settings_handler.h"
#include "cef/task_manager_handler.h"
#include "core/closed_tabs.h"
#include "core/download_paths.h"
#include "core/launch_args.h"
#include "core/profile_registry.h"
#include "core/session.h"
#include "core/yaml_value.h"
#include "include/cef_request_context.h"
#include "platform/win/browser_window.h"
#include "platform/win/devtools_window.h"
#include "platform/win/export_password_dialog.h"
#include "platform/win/internal_page_window.h"
#include "platform/win/settings_watcher.h"
#include "platform/win/system_events.h"

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
  // Developer tools (006), for the windows (BrowserWindowOwner).
  DevToolsPreferences* DevToolsPrefs() override;
  std::string DockPosition() override;
  int DockSize() override;
  void SetDockPosition(const std::string& position) override;
  void SetDockSize(int size) override;
  bool DevToolsToggleOnLeft() override;
  std::string SecretHeaderNames() override;
  ToolsHost::Rules RequestRules(const ProfileId& profile) override;
  std::optional<Error> SetRequestRules(const ProfileId& profile,
                                       RequestRuleSet rules) override;
  void BrowserCount(int delta) override;
  void InspectPage(CefRefPtr<CefBrowser> page,
                   const std::wstring& title,
                   std::optional<POINT> at) override;
  // devtools.remote_debugging (006), read once before CEF starts: the port,
  // or 0 for none.
  int RemoteDebuggingPort() const;
  // Ad blocking (007): a site on or off a profile's allowlist (the shield,
  // the blocked page), and a rule added to My filters (`done` after).
  void SetAdblockSite(const std::string& host,
                      bool allowed,
                      const ProfileId& profile) override;
  void AddAdblockRule(const std::string& rule,
                      std::function<void()> done) override;
  // A profile whose passwords come from Bitwarden (008).
  bool ProfileUsesBitwarden(const ProfileId& profile);
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
  FaviconCache* Favicons() override;
  void OnHistoryChanged(const ProfileId& profile) override;
  const std::vector<HistoryMatch>* RecentPages(
      const ProfileId& profile) override;
  std::string SearchSuggestTemplate() override;
  bool HistorySuggestionsEnabled() override;
  DownloadService* Downloads() override;
  PasswordManager* Passwords() override;
  BookmarkStore* Bookmarks() override;
  bool BookmarkSuggestionsEnabled() override;
  bool BookmarksBarAlwaysShown() override;
  void SetBookmarksBarAlwaysShown(bool always) override;
  void OpenTaskManager(const std::string& row) override;
  std::vector<BrowserWindow*> NormalWindows() override;
  const std::vector<std::pair<std::string, std::string>>& HelpSections()
      override;
  // For the task manager (020): every tab of every window (in the order
  // the windows were opened) and Brouk's page windows, and acting on one.
  std::vector<TaskManagerPage> TaskPages();
  void ActOnTaskPage(const std::string& key, TaskPageAction action);
  bool AskWhereToSaveDownloads() override;
  DownloadTarget ReserveDownloadPath(const std::string& suggested,
                                     const ProfileId& profile) override;
  void ReleaseDownloadPath(const std::string& path) override;
  // The downloads page (014) follows the list while it is open: `changed`
  // runs after every change; the id removes it again.
  int WatchDownloads(std::function<void()> changed);
  void UnwatchDownloads(int id);
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
  bool OnWindowClosing(BrowserWindow& window) override;
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
  // Quitting would cancel running downloads (014): asks first, as Chrome.
  // True to go on.
  bool ConfirmQuitWithDownloads(HWND owner);
  // The windows' downloads buttons and the open downloads pages follow.
  void DownloadsChanged();
  // The windows' stars follow; bookmarked pages keep their icons in the
  // history.
  void BookmarksChanged();
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
  std::array<std::unique_ptr<InternalPageWindow>*, 4> PageWindows();
  // A settings file changed on disk (004, live reload).
  void SettingsFilesChanged();
  // Applies the global settings Brouk on Windows has so far: theme and
  // the JSON viewer.
  void ApplySettings();
  // The window the settings and profile pages' dialogs belong to.
  HWND DialogOwner() const;
  void SetUpBitwarden();
  // "Inspect" in one of Brouk's page windows: InspectPage.
  std::function<void(CefRefPtr<CefBrowser>, const std::wstring&, POINT)>
  PageInspector();
  // Ad blocking (007): the lists and category sets in use, and each
  // profile's policy, from the settings.
  void ConfigureAdblock();
  void UpdateAdblockPolicies();
  // Profile export (005) with `passwords` (the encrypted section, or none):
  // asks where to save and writes the file.
  void WriteProfileExport(const ProfileId& id,
                          const YamlNode& passwords,
                          ExportDone done);
  // The export password dialog (008), one at a time.
  void AskExportPassword(HWND owner,
                         bool confirm,
                         std::wstring message,
                         ExportPasswordDialog::Done done);
  // Profile import (005): the file's passwords go into the new profile once
  // the user types the right export password.
  void ImportProfilePasswordsInto(const ProfileId& profile,
                                  YamlNode section,
                                  std::wstring message,
                                  std::vector<std::string> problems,
                                  ImportDone done);

  ProfileRegistry profiles_;
  // One per profile, made when its first tab opens.
  std::map<ProfileId, CefRefPtr<CefRequestContext>> contexts_;
  CefRefPtr<CefRequestContext> internal_context_;
  std::unique_ptr<InternalPageWindow> help_window_;
  std::unique_ptr<InternalPageWindow> profiles_window_;
  std::unique_ptr<InternalPageWindow> settings_window_;
  std::unique_ptr<InternalPageWindow> task_manager_window_;
  // help/sections.json, read when the command palette first needs it.
  std::optional<std::vector<std::pair<std::string, std::string>>>
      help_sections_;
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
  // Windows that started closing shortly before the last one: the
  // taskbar's "Close all windows" closes them one by one, and they all
  // come back at the next start, as with Exit (015).
  struct ClosingWindow {
    const BrowserWindow* window = nullptr;  // Compared only, never used.
    ULONGLONG closed_at = 0;
    SessionWindow state;
  };
  std::vector<ClosingWindow> closing_windows_;
  // Tabs and windows closed in this run (013).
  ClosedTabList closed_tabs_;
  // History (013): one database per profile, on its own thread; each
  // profile's latest pages for the History menu, read after each visit.
  std::unique_ptr<HistoryService> history_;
  std::unique_ptr<FaviconCache> favicons_;
  std::map<ProfileId, std::vector<HistoryMatch>> recent_pages_;
  // Hidden helper browsers still open (clearing data), and developer tools'
  // browsers; Brouk quits after they have closed.
  int helper_browsers_ = 0;
  int devtools_browsers_ = 0;
  // Developer tools (006): DevTools' own settings, and its windows for
  // Brouk's pages.
  std::unique_ptr<DevToolsPreferences> devtools_preferences_;
  std::vector<std::unique_ptr<DevToolsWindow>> devtools_windows_;
  // Bookmarks (011), for the whole app; made when CEF is ready.
  std::unique_ptr<BookmarkStore> bookmarks_;
  // Downloads (014): every download of the app, recorded in the history;
  // unique paths of the running ones; open downloads pages.
  std::unique_ptr<DownloadService> downloads_;
  DownloadPathReserver download_paths_;
  std::map<int, std::function<void()>> download_watchers_;
  int next_download_watcher_ = 1;
  // Built-in passwords (008), for every profile; made when CEF is ready.
  std::unique_ptr<PasswordManager> passwords_;
  std::unique_ptr<ExportPasswordDialog> export_password_dialog_;
  // Ad blocking (007), for every profile; made when CEF is ready. The lists
  // and categories it was last configured with.
  std::unique_ptr<AdblockService> adblock_;
  std::string adblock_configuration_;
  // Form autofill (009), for the whole app; made when CEF is ready.
  std::unique_ptr<AutofillManager> autofill_;
  // Locks Bitwarden when the PC sleeps or the session is locked.
  std::unique_ptr<SystemEvents> system_events_;
  std::vector<std::unique_ptr<BrowserWindow>> windows_;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_APP_H_
