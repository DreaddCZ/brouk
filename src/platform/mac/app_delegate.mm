#import "platform/mac/app_delegate.h"

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#import "platform/mac/bookmark_store.h"
#import "platform/mac/browser_window_controller.h"
#import "platform/mac/data_paths.h"
#import "platform/mac/internal_page_window.h"
#import "platform/mac/menu_shortcuts.h"
#import "platform/mac/profile_colors.h"
#import "platform/mac/settings_watcher.h"

#include "brouk/app_info.h"
#include "cef/adblock_service.h"
#include "cef/bookmarks_page_handler.h"
#include "cef/browser_app.h"
#include "cef/browsing_data.h"
#include "cef/devtools_preferences.h"
#include "cef/history_page_handler.h"
#include "cef/history_service.h"
#include "cef/internal_scheme.h"
#include "cef/profile_manager_handler.h"
#include "cef/settings_handler.h"
#include "core/adblock_lists.h"
#include "core/address_input.h"
#include "core/bookmark_profile.h"
#include "core/download_paths.h"
#include "core/profile_export.h"
#include "core/profile_registry.h"
#include "core/request_rules.h"
#include "core/search_engines.h"
#include "core/session.h"
#include "core/settings_export.h"
#include "core/shortcuts.h"
#include "core/url_pattern.h"
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/cef_request_context.h"
#include "include/wrapper/cef_helpers.h"

@implementation BRApplication {
  BOOL _handlingSendEvent;
}

- (BOOL)isHandlingSendEvent {
  return _handlingSendEvent;
}

- (void)setHandlingSendEvent:(BOOL)handlingSendEvent {
  _handlingSendEvent = handlingSendEvent;
}

- (void)sendEvent:(NSEvent*)event {
  CefScopedSendingEvent sendingEventScoper;
  [super sendEvent:event];
}

// Quitting must wait until CEF has closed every browser; the delegate drives
// that and calls CefQuitMessageLoop() at the end.
- (void)terminate:(id)sender {
  [static_cast<BRAppDelegate*>(self.delegate) tryToTerminateApplication];
}

@end

@interface BRAppDelegate () <BRBrowserWindowOwner,
                             BRInternalPageOwner,
                             NSMenuDelegate>
- (std::vector<brouk::ProfileManagerDelegate::Summary>)profileSummaries;
- (brouk::Result<brouk::ProfileId>)createProfileNamed:(const std::string&)name
                                                color:(const std::string&)color;
- (std::optional<brouk::Error>)updateProfile:(const brouk::ProfileId&)profile
                                        name:(const std::string&)name
                                       color:(const std::string&)color;
- (std::optional<brouk::Error>)makeDefaultProfile:
    (const brouk::ProfileId&)profile;
- (std::optional<brouk::Error>)setShowProfileManagerAtStartup:(BOOL)show;
- (BOOL)showProfileManagerAtStartup;
- (std::optional<brouk::Error>)openProfile:(const brouk::ProfileId&)profile;
- (void)deleteProfile:(const brouk::ProfileId&)profile
                 done:(brouk::ProfileManagerDelegate::DeleteDone)done;
- (void)exportProfile:(const brouk::ProfileId&)profile
                 done:(brouk::ProfileManagerDelegate::ExportDone)done;
- (void)importProfileWithDone:(brouk::ProfileManagerDelegate::ImportDone)done;
- (std::optional<brouk::Error>)setGlobalSetting:(std::string_view)key
                                          value:(brouk::SettingValue)value;
- (std::optional<brouk::Error>)setProfileSetting:(std::string_view)key
                                         profile:(std::string_view)profile
                                           value:(brouk::SettingValue)value;
- (void)revealSettingsFolder;
- (IBAction)clearBrowsingData:(nullable id)sender;
- (void)exportSettingsWithDone:(brouk::SettingsPageDelegate::FileDone)done;
- (void)importSettingsWithDone:(brouk::SettingsPageDelegate::FileDone)done;
- (std::vector<brouk::ProfileInfo>)allProfiles;
- (std::vector<std::string>)keepListOfProfile:(const brouk::ProfileId&)profile;
- (void)previewDataOfProfile:(const brouk::ProfileId&)profile
                       since:(std::int64_t)since
                        done:(brouk::HistoryPageDelegate::DataPreviewDone)done;
- (void)clearDataOfProfile:(const brouk::ProfileId&)profile
                     since:(std::int64_t)since
                   cookies:(bool)cookies
                     cache:(bool)cache
                     sites:(std::set<std::string>)sites
                      done:(std::function<void(bool)>)done;
- (std::string)historyView;
@end

namespace brouk {

// Answers the profile manager page with the app delegate's profiles and
// windows.
class AppProfileManager : public ProfileManagerDelegate {
 public:
  explicit AppProfileManager(BRAppDelegate* app) : app_(app) {}

  // Messages to nil returning C++ objects do not construct them, so every
  // call checks the (weak) app delegate first.
  std::vector<Summary> Profiles() override {
    BRAppDelegate* app = app_;
    return app ? [app profileSummaries] : std::vector<Summary>();
  }
  bool ShowAtStartup() override {
    BRAppDelegate* app = app_;
    return app && [app showProfileManagerAtStartup];
  }
  Result<ProfileId> CreateProfile(const std::string& name,
                                  const std::string& color) override {
    BRAppDelegate* app = app_;
    if (!app) {
      return Error{kClosing};
    }
    return [app createProfileNamed:name color:color];
  }
  std::optional<Error> UpdateProfile(const ProfileId& id,
                                     const std::string& name,
                                     const std::string& color) override {
    BRAppDelegate* app = app_;
    return app ? [app updateProfile:id name:name color:color]
               : std::optional<Error>(Error{kClosing});
  }
  std::optional<Error> SetDefaultProfile(const ProfileId& id) override {
    BRAppDelegate* app = app_;
    return app ? [app makeDefaultProfile:id]
               : std::optional<Error>(Error{kClosing});
  }
  std::optional<Error> SetShowAtStartup(bool show) override {
    BRAppDelegate* app = app_;
    return app ? [app setShowProfileManagerAtStartup:show]
               : std::optional<Error>(Error{kClosing});
  }
  std::optional<Error> OpenProfile(const ProfileId& id) override {
    BRAppDelegate* app = app_;
    return app ? [app openProfile:id] : std::optional<Error>(Error{kClosing});
  }
  void DeleteProfile(const ProfileId& id, DeleteDone done) override {
    BRAppDelegate* app = app_;
    if (!app) {
      done(false, Error{kClosing});
      return;
    }
    [app deleteProfile:id done:std::move(done)];
  }
  void ExportProfile(const ProfileId& id, ExportDone done) override {
    BRAppDelegate* app = app_;
    if (!app) {
      done(false, Error{kClosing});
      return;
    }
    [app exportProfile:id done:std::move(done)];
  }
  void ImportProfile(ImportDone done) override {
    BRAppDelegate* app = app_;
    if (!app) {
      done(Error{kClosing});
      return;
    }
    [app importProfileWithDone:std::move(done)];
  }

 private:
  static constexpr char kClosing[] = "Brouk is closing.";
  __weak BRAppDelegate* app_;
};

// Answers one history tab's page (013). Actions go to the (weak) app
// delegate and the window holding the tab.
class AppHistoryPage : public HistoryPageDelegate {
 public:
  AppHistoryPage(BRAppDelegate* app,
                 BRBrowserWindowController* controller,
                 TabId tab)
      : app_(app), controller_(controller), tab_(tab) {}

  std::vector<ProfileInfo> Profiles() override {
    BRAppDelegate* app = app_;
    return app ? [app allProfiles] : std::vector<ProfileInfo>{};
  }
  HistoryService* History() override { return [app_ historyService]; }
  void OpenPage(const ProfileId& profile,
                const std::string& url,
                bool new_tab) override {
    [controller_ openPage:@(url.c_str())
                  profile:profile
                   newTab:new_tab
              fromHistory:tab_];
  }
  std::string View() override {
    BRAppDelegate* app = app_;
    return app ? [app historyView] : "site";
  }
  ProfileId CurrentProfile() override {
    return [controller_ profileOfTab:tab_];
  }
  std::vector<std::string> KeepList(const ProfileId& profile) override {
    BRAppDelegate* app = app_;
    return app ? [app keepListOfProfile:profile] : std::vector<std::string>{};
  }
  void SetKeepList(const ProfileId& profile,
                   const std::vector<std::string>& hosts) override {
    std::string joined;
    for (const std::string& host : hosts) {
      joined += joined.empty() ? host : ", " + host;
    }
    [app_ setProfileSetting:"history.keep_list"
                    profile:profile
                      value:SettingValue(joined)];
  }
  void PreviewProfileData(const ProfileId& profile,
                          std::int64_t since,
                          DataPreviewDone done) override {
    BRAppDelegate* app = app_;
    if (!app) {
      done(0, 0);
      return;
    }
    [app previewDataOfProfile:profile since:since done:std::move(done)];
  }
  void ClearProfileData(const ProfileId& profile,
                        std::int64_t since,
                        bool cookies_and_site_data,
                        bool cache,
                        std::set<std::string> sites,
                        std::function<void(bool)> done) override {
    BRAppDelegate* app = app_;
    if (!app) {
      done(false);
      return;
    }
    [app clearDataOfProfile:profile
                      since:since
                    cookies:cookies_and_site_data
                      cache:cache
                      sites:std::move(sites)
                       done:std::move(done)];
  }
  void CopyText(const std::string& text) override {
    NSPasteboard* pasteboard = NSPasteboard.generalPasteboard;
    [pasteboard clearContents];
    [pasteboard setString:@(text.c_str()) ?: @""
                  forType:NSPasteboardTypeString];
  }
  void SetView(const std::string& view) override {
    [app_ setGlobalSetting:"history.view" value:SettingValue(view)];
  }

 private:
  __weak BRAppDelegate* app_;
  __weak BRBrowserWindowController* controller_;
  TabId tab_;
};

// Answers one bookmark manager tab's page (011): the app's bookmarks, and
// opening and file panels through the window holding the tab.
class AppBookmarksPage : public BookmarksPageDelegate {
 public:
  AppBookmarksPage(BRAppDelegate* app,
                   BRBookmarkStore* store,
                   BRBrowserWindowController* controller,
                   TabId tab)
      : app_(app), store_(store), controller_(controller), tab_(tab) {}
  ~AppBookmarksPage() override {
    if (observer_) {
      [NSNotificationCenter.defaultCenter removeObserver:observer_];
    }
  }
  AppBookmarksPage(const AppBookmarksPage&) = delete;
  AppBookmarksPage& operator=(const AppBookmarksPage&) = delete;

  std::vector<ProfileInfo> Profiles() override {
    BRAppDelegate* app = app_;
    return app ? [app allProfiles] : std::vector<ProfileInfo>{};
  }
  ProfileId CurrentProfile() override {
    return [controller_ profileOfTab:tab_];
  }
  const BookmarkModel& Model() override { return [store_ model]; }
  void Change(const std::function<void(BookmarkModel&)>& change) override {
    [store_ change:change];
  }
  void Remove(const std::vector<BookmarkId>& ids) override {
    [store_ removeItems:ids];
  }
  bool Undo() override {
    if (![store_ canUndoRemove]) {
      return false;
    }
    [store_ undoRemove];
    return true;
  }
  // A general bookmark opens in the manager tab's profile, a linked one in
  // its own (a new tab when the manager tab is in another profile).
  void Open(BookmarkId id, bool new_tab) override {
    const BookmarkItem* item = [store_ model].Find(id);
    if (item == nullptr || item->folder) {
      return;
    }
    ProfileId profile = [store_ model].EffectiveProfile(id);
    [controller_ openPage:@(item->url.c_str())
                  profile:profile
                   newTab:new_tab
                  fromTab:tab_
                 bookmark:YES];
  }
  void Watch(std::function<void()> changed) override {
    if (observer_) {
      [NSNotificationCenter.defaultCenter removeObserver:observer_];
    }
    observer_ = [NSNotificationCenter.defaultCenter
        addObserverForName:BRBookmarksDidChangeNotification
                    object:nil
                     queue:nil
                usingBlock:^(NSNotification*) {
                  changed();
                }];
  }
  void SaveHtml(std::string html, SaveDone done) override {
    NSWindow* window = controller_.window;
    if (!window) {
      done(false, std::nullopt);
      return;
    }
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.title = @"Export Bookmarks";
    panel.nameFieldStringValue = @"Bookmarks.html";
    panel.allowedContentTypes = @[ UTTypeHTML ];
    __block SaveDone answer = std::move(done);
    __block std::string text = std::move(html);
    [panel
        beginSheetModalForWindow:window
               completionHandler:^(NSModalResponse response) {
                 if (response != NSModalResponseOK || !panel.URL) {
                   answer(false, std::nullopt);
                   return;
                 }
                 NSData* data = [NSData dataWithBytes:text.data()
                                               length:text.size()];
                 NSError* error = nil;
                 if (![data writeToURL:panel.URL
                               options:NSDataWritingAtomic
                                 error:&error]) {
                   answer(false,
                          Error{std::string("The file could not be saved: ") +
                                error.localizedDescription.UTF8String});
                   return;
                 }
                 answer(true, std::nullopt);
               }];
  }
  void OpenHtml(OpenDone done) override {
    NSWindow* window = controller_.window;
    if (!window) {
      done(std::nullopt, std::nullopt);
      return;
    }
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.title = @"Import Bookmarks";
    panel.message = @"An HTML bookmark file, as exported by Chrome, Firefox "
                    @"or Safari. The bookmarks go into \"Imported\" under "
                    @"Other bookmarks.";
    panel.allowedContentTypes = @[ UTTypeHTML ];
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    __block OpenDone answer = std::move(done);
    [panel beginSheetModalForWindow:window
                  completionHandler:^(NSModalResponse response) {
                    if (response != NSModalResponseOK || !panel.URL) {
                      answer(std::nullopt, std::nullopt);
                      return;
                    }
                    NSError* error = nil;
                    NSData* data = [NSData dataWithContentsOfURL:panel.URL
                                                         options:0
                                                           error:&error];
                    if (!data) {
                      answer(std::nullopt,
                             Error{std::string("The file could not be read: ") +
                                   error.localizedDescription.UTF8String});
                      return;
                    }
                    answer(std::string(static_cast<const char*>(data.bytes),
                                       data.length),
                           std::nullopt);
                  }];
  }

 private:
  __weak BRAppDelegate* app_;
  BRBookmarkStore* store_;
  __weak BRBrowserWindowController* controller_;
  TabId tab_;
  id observer_;
};

// Answers the settings page. The registry outlives the page; actions go to
// the (weak) app delegate.
class AppSettingsPage : public SettingsPageDelegate {
 public:
  AppSettingsPage(BRAppDelegate* app, ProfileRegistry& registry)
      : app_(app), registry_(registry) {}

  const ProfileRegistry& Registry() override { return registry_; }
  std::optional<Settings> ProfileSettings(std::string_view id) override {
    return registry_.ProfileSettings(id);
  }
  std::optional<Error> SetGlobalSetting(std::string_view key,
                                        SettingValue value) override {
    BRAppDelegate* app = app_;
    return app ? [app setGlobalSetting:key value:std::move(value)]
               : std::optional<Error>(Error{kClosing});
  }
  std::optional<Error> SetProfileSetting(std::string_view id,
                                         std::string_view key,
                                         SettingValue value) override {
    BRAppDelegate* app = app_;
    return app ? [app setProfileSetting:key profile:id value:std::move(value)]
               : std::optional<Error>(Error{kClosing});
  }
  void RevealSettingsFolder() override { [app_ revealSettingsFolder]; }
  void OpenClearBrowsingData() override { [app_ clearBrowsingData:nil]; }
  void ExportSettings(FileDone done) override {
    BRAppDelegate* app = app_;
    if (!app) {
      done(false, {}, Error{kClosing});
      return;
    }
    [app exportSettingsWithDone:std::move(done)];
  }
  void ImportSettings(FileDone done) override {
    BRAppDelegate* app = app_;
    if (!app) {
      done(false, {}, Error{kClosing});
      return;
    }
    [app importSettingsWithDone:std::move(done)];
  }

 private:
  static constexpr char kClosing[] = "Brouk is closing.";
  __weak BRAppDelegate* app_;
  ProfileRegistry& registry_;
};

}  // namespace brouk

@implementation BRAppDelegate {
  NSMutableArray<BRBrowserWindowController*>* _windows;
  // Profiles (005); loaded at launch, before CEF starts.
  std::unique_ptr<brouk::ProfileRegistry> _profiles;
  // History databases (013); from cefContextInitialized to shutdown.
  std::unique_ptr<brouk::HistoryService> _history;
  // Ad and tracker blocking (007), same lifetime; reconfigured only when
  // the lists or the profiles' categories change.
  std::unique_ptr<brouk::AdblockService> _adblock;
  std::string _adblockConfiguration;
  // History menu (013): tabs and windows closed in this run, and each
  // profile's recently visited pages (fetched from its history).
  brouk::ClosedTabList _closedTabs;
  std::map<brouk::ProfileId, std::vector<brouk::HistoryMatch>, std::less<>>
      _recentPages;
  NSMenu* _profilesMenu;
  // Session restore (015): saved shortly after changes while running (not
  // before the start has restored, not after quitting began).
  std::filesystem::path _sessionPath;
  NSTimer* _sessionTimer;
  BOOL _sessionReady;
  BOOL _sessionFrozen;
  NSMenu* _recentlyClosedMenu;
  NSMenu* _recentlyVisitedMenu;
  // Bookmarks (011): the one tree of the app, and the Bookmarks menu, whose
  // first items are fixed and the rest filled when it opens.
  BRBookmarkStore* _bookmarks;
  NSMenu* _bookmarksMenu;
  NSInteger _bookmarksMenuFixedItems;
  // Each profile's request context (its isolated web data), created when the
  // profile is first used.
  std::map<brouk::ProfileId, CefRefPtr<CefRequestContext>> _requestContexts;
  BOOL _profilesLoaded;
  BRSettingsWatcher* _settingsWatcher;
  int _remoteDebuggingPort;  // 0: off.
  std::unique_ptr<brouk::DevToolsPreferences> _devToolsPreferences;
  // Internal pages (brouk://) load in their own in-memory request context,
  // never in a profile's.
  CefRefPtr<CefRequestContext> _internalContext;
  std::unique_ptr<brouk::AppProfileManager> _profileManagerDelegate;
  BRInternalPageWindowController* _profileManager;
  BRInternalPageWindowController* _settingsWindow;
  std::unique_ptr<brouk::AppSettingsPage> _settingsPageDelegate;
  // Shown instead of a window at startup: opening a profile closes it.
  BOOL _profileManagerAtStartup;
  int _browserCount;
  BOOL _quitting;
  std::unique_ptr<brouk::DownloadPathReserver> _downloadPaths;
}

- (instancetype)init {
  if ((self = [super init])) {
    _windows = [NSMutableArray array];
    _profiles = std::make_unique<brouk::ProfileRegistry>(
        brouk::mac::AppDataDirectory().fileSystemRepresentation);
    _profiles->Load();
    // Web data of profiles deleted earlier; Chromium is not running yet.
    _profiles->RemoveWebDataOfDeletedProfiles(
        brouk::mac::WebDataRootDirectory().fileSystemRepresentation);
    _bookmarks = [[BRBookmarkStore alloc]
        initWithFile:[brouk::mac::AppDataDirectory()
                         stringByAppendingPathComponent:@"bookmarks.json"]];
    _profileManagerDelegate = std::make_unique<brouk::AppProfileManager>(self);
    _settingsPageDelegate =
        std::make_unique<brouk::AppSettingsPage>(self, *_profiles);
    _downloadPaths =
        std::make_unique<brouk::DownloadPathReserver>([](const std::string& p) {
          return [NSFileManager.defaultManager
              fileExistsAtPath:[NSString stringWithUTF8String:p.c_str()]];
        });
  }
  return self;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  NSApp.mainMenu = [self buildMainMenu];
  // The settings may have been read already (CEF can be ready first).
  brouk::mac::ApplyShortcutsToMenu(NSApp.mainMenu);
  // Our tab strip replaces macOS window tabs.
  NSWindow.allowsAutomaticWindowTabbing = NO;
}

- (void)cefContextInitialized {
  CEF_REQUIRE_UI_THREAD();
  _profilesLoaded = YES;
  std::filesystem::path profilesDir =
      std::filesystem::path(brouk::mac::AppDataDirectory().UTF8String) /
      "profiles";
  _history = std::make_unique<brouk::HistoryService>(
      [profilesDir](const brouk::ProfileId& profile) {
        return profilesDir / profile / "history.db";
      });
  NSString* resources = [NSString
      stringWithContentsOfFile:[NSBundle.mainBundle
                                   pathForResource:@"adblock-resources"
                                            ofType:@"json"]
                      encoding:NSUTF8StringEncoding
                         error:nil];
  _adblock = std::make_unique<brouk::AdblockService>(
      std::filesystem::path(brouk::mac::AppDataDirectory().UTF8String) /
          "adblock",
      resources.UTF8String ?: "");
  [self configureAdblock];
  // Remote debugging (006) is fixed for this run: from the setting (passed
  // to CefInitialize) or the --remote-debugging-port switch.
  CefRefPtr<CefCommandLine> commandLine =
      CefCommandLine::GetGlobalCommandLine();
  if (commandLine->HasSwitch("remote-debugging-port")) {
    _remoteDebuggingPort =
        std::atoi(commandLine->GetSwitchValue("remote-debugging-port")
                      .ToString()
                      .c_str());
  } else {
    _remoteDebuggingPort = [self remoteDebuggingPortSetting];
  }
  [self reportProfileProblems];
  [self applySettings];
  __weak BRAppDelegate* weakSelf = self;
  _settingsWatcher = [[BRSettingsWatcher alloc]
      initWithDirectory:brouk::mac::AppDataDirectory()
                changed:^{
                  [weakSelf settingsFilesChanged];
                }];
  // Like Chrome, URLs given on the command line open as tabs.
  brouk::LaunchRequest request = brouk::BrowserApp::LaunchRequestFrom(
      CefCommandLine::GetGlobalCommandLine());
  brouk::Session last = [self startSession];
  brouk::StartupAction action = brouk::DecideStartup(
      _profiles->global_settings().GetString("startup.restore"), last);
  if (action == brouk::StartupAction::kRestore) {
    [self restoreSession:last];
    if (!request.urls.empty()) {
      [self openLaunchRequest:request inController:[self frontController]];
    }
    _sessionReady = YES;
    return;
  }
  _sessionReady = YES;
  if (_profiles->show_manager_at_startup() && request.urls.empty() &&
      !request.profile) {
    _profileManagerAtStartup = YES;
    [self manageProfiles:nil];
    return;
  }
  [self openLaunchRequest:request inController:nil];
  if (action == brouk::StartupAction::kNewWindowOfferRestore) {
    __weak BRAppDelegate* weakDelegate = self;
    BRBrowserWindowController* front = [self frontController];
    __weak BRBrowserWindowController* weakFront = front;
    [front showRestorePagesBar:^{
      [weakDelegate restoreSession:last];
      // The new tab the start opened is not needed any more (Chrome).
      BRBrowserWindowController* blank = weakFront;
      if (blank.hasOnlyNewTab) {
        [blank closeAllTabs];
      }
    }];
  }
}

#pragma mark - Session restore (015)

// Main thread. Reads the last run's session and marks this run as running
// (clean_exit: false), so a crash is noticed at the next start.
- (brouk::Session)startSession {
  _sessionPath =
      std::filesystem::path(brouk::mac::AppDataDirectory().UTF8String) /
      "session.yaml";
  brouk::Result<brouk::Session> loaded = brouk::LoadSession(_sessionPath);
  if (!loaded) {
    NSLog(@"Session: %s", loaded.error().message.c_str());
  }
  brouk::Session last = loaded ? loaded.value() : brouk::Session{};
  brouk::Session running = last;
  running.clean_exit = false;
  if (std::optional<brouk::Error> error =
          brouk::SaveSession(_sessionPath, running)) {
    NSLog(@"Session: %s", error->message.c_str());
  }
  return last;
}

- (void)restoreSession:(const brouk::Session&)session {
  // Saved back to front: the last one restored ends up in front.
  for (const brouk::SessionWindow& window : session.windows) {
    BRBrowserWindowController* controller = [self openWindow];
    [controller restoreSessionWindow:window];
    [controller showWindow:nil];
  }
}

- (void)browserWindowSessionChanged {
  if (!_sessionReady || _sessionFrozen || _sessionTimer) {
    return;
  }
  __weak BRAppDelegate* weakSelf = self;
  _sessionTimer = [NSTimer
      scheduledTimerWithTimeInterval:1.5
                             repeats:NO
                               block:^(NSTimer* /*timer*/) {
                                 BRAppDelegate* strongSelf = weakSelf;
                                 if (strongSelf) {
                                   strongSelf->_sessionTimer = nil;
                                   [strongSelf saveSessionWithCleanExit:NO];
                                 }
                               }];
}

// Main thread. The open browser windows, back to front.
- (void)saveSessionWithCleanExit:(BOOL)cleanExit {
  if (_sessionPath.empty()) {
    return;
  }
  brouk::Session session;
  session.clean_exit = cleanExit;
  for (NSWindow* window in NSApp.orderedWindows.reverseObjectEnumerator) {
    for (BRBrowserWindowController* controller in _windows) {
      if (controller.window != window) {
        continue;
      }
      if (std::optional<brouk::SessionWindow> saved =
              [controller sessionWindow]) {
        session.windows.push_back(std::move(*saved));
      }
    }
  }
  if (std::optional<brouk::Error> error =
          brouk::SaveSession(_sessionPath, session)) {
    NSLog(@"Session: %s", error->message.c_str());
  }
}

- (BOOL)profileExists:(const brouk::ProfileId&)profile {
  return _profiles->Find(profile) != nullptr;
}

- (void)relaunchedWithRequest:(const brouk::LaunchRequest&)request {
  CEF_REQUIRE_UI_THREAD();
  if (_quitting || !_profilesLoaded) {
    return;
  }
  [NSApp activateIgnoringOtherApps:YES];
  // Chrome adds the URLs as tabs to the front window; without URLs it opens
  // a new window.
  [self openLaunchRequest:request
             inController:request.urls.empty() ? nil : [self frontController]];
}

// Main thread. The front normal (non-popup) browser window, if any.
- (nullable BRBrowserWindowController*)frontController {
  for (NSWindow* window in NSApp.orderedWindows) {
    for (BRBrowserWindowController* controller in _windows) {
      if (controller.window == window && !controller.isPopup) {
        return controller;
      }
    }
  }
  return nil;
}

// Main thread. Opens a launch's URLs as tabs of its profile (the default
// profile if none or an unknown one is named), in `controller` or a new
// window; an empty new tab when none of the URLs is usable.
- (void)openLaunchRequest:(const brouk::LaunchRequest&)request
             inController:(nullable BRBrowserWindowController*)controller {
  brouk::ProfileId profile = _profiles->default_profile().id;
  if (request.profile) {
    const brouk::ProfileInfo* named = _profiles->FindByName(*request.profile);
    if (!named) {
      named = _profiles->Find(*request.profile);
    }
    if (named) {
      profile = named->id;
    } else {
      NSLog(@"No profile called \"%s\"; opening the default profile.",
            request.profile->c_str());
    }
  }
  BRBrowserWindowController* target = controller ?: [self openWindow];
  NSUInteger opened = 0;
  for (const std::string& url : request.urls) {
    brouk::AddressInputResult input = brouk::ClassifyAddressInput(url);
    if (input.kind != brouk::AddressInputResult::Kind::kEmpty) {
      [target openTabWithURL:@(input.url.c_str()) profile:profile activate:YES];
      ++opened;
    }
  }
  if (opened == 0 && !controller) {
    [target openTabWithURL:nil profile:profile activate:YES];
  }
  [target showWindow:nil];
}

// A settings file changed (by hand or by us): re-read the profiles and
// redraw names, colours and the default profile's place (004 live reload).
- (void)settingsFilesChanged {
  _profiles->Load();
  [self reportProfileProblems];
  [self applySettings];
  [self reloadRequestRules];
  for (BRBrowserWindowController* controller in _windows) {
    [controller profilesDidChange];
  }
}

// Problems in the settings files are reported, not ignored (004). Until the
// settings page exists they go to the log.
- (void)reportProfileProblems {
  for (const std::string& problem : _profiles->problems()) {
    NSLog(@"Settings: %s", problem.c_str());
  }
}

#pragma mark - BRBrowserWindowOwner

- (CefRefPtr<CefRequestContext>)requestContextForProfile:
    (const brouk::ProfileId&)profile {
  CEF_REQUIRE_UI_THREAD();
  auto it = _requestContexts.find(profile);
  if (it != _requestContexts.end()) {
    return it->second;
  }
  // Ids are validated when profiles are read; never build a path from
  // anything else.
  brouk::ProfileId id = brouk::IsValidProfileId(profile)
                            ? profile
                            : _profiles->default_profile().id;
  CefRequestContextSettings settings;
  CefString(&settings.cache_path) =
      brouk::mac::ProfileWebDataDirectory(@(id.c_str())).UTF8String;
  CefRefPtr<CefRequestContext> context =
      CefRequestContext::CreateContext(settings, nullptr);
  _requestContexts[profile] = context;
  [self applySpellcheckTo:context];
  return context;
}

- (brouk::ProfileId)defaultProfileId {
  return _profiles->default_profile().id;
}

- (brouk::ProfileInfo)profileInfo:(const brouk::ProfileId&)profile {
  if (const brouk::ProfileInfo* info = _profiles->Find(profile)) {
    return *info;
  }
  return {profile, profile, "grey"};
}

- (void)browserWindowDidCreateBrowser {
  ++_browserCount;
}

- (void)browserWindowDidCloseBrowser {
  --_browserCount;
  [self quitIfDone];
}

- (void)browserWindowWillClose:(BRBrowserWindowController*)controller {
  // Keep the controller alive until AppKit is done with the close.
  dispatch_async(dispatch_get_main_queue(), ^{
    [self->_windows removeObject:controller];
    [self browserWindowSessionChanged];
    [self quitIfDone];
  });
}

#pragma mark - Windows and quitting

- (BRBrowserWindowController*)openWindow {
  BRBrowserWindowController* controller =
      [[BRBrowserWindowController alloc] initWithOwner:self];
  controller.settingsProblemCount = _profiles->problems().size();
  [self applyDevToolsSettingsTo:controller];
  [_windows addObject:controller];
  [controller showWindow:nil];
  return controller;
}

- (void)openWindowWithURL:(NSString*)url
                  profile:(const brouk::ProfileId&)profile {
  if (_quitting || !_profilesLoaded) {
    return;
  }
  [[self openWindow] openTabWithURL:url profile:profile activate:YES];
}

- (std::string)reserveDownloadPath:(const std::string&)suggested {
  // The folder from the settings (004) if it exists; otherwise Chrome's
  // default, ~/Downloads.
  const std::string& configured =
      _profiles->global_settings().GetString("downloads.folder");
  if (!configured.empty()) {
    NSString* folder = @(configured.c_str()).stringByExpandingTildeInPath;
    BOOL isDirectory = NO;
    if ([NSFileManager.defaultManager fileExistsAtPath:folder
                                           isDirectory:&isDirectory] &&
        isDirectory) {
      return _downloadPaths->Reserve(folder.UTF8String, suggested);
    }
    NSLog(@"Download folder %@ does not exist; using Downloads.", folder);
  }
  NSURL* downloads =
      [NSFileManager.defaultManager URLForDirectory:NSDownloadsDirectory
                                           inDomain:NSUserDomainMask
                                  appropriateForURL:nil
                                             create:YES
                                              error:nil];
  NSString* dir =
      downloads.path
          ?: [NSHomeDirectory() stringByAppendingPathComponent:@"Downloads"];
  return _downloadPaths->Reserve(dir.UTF8String, suggested);
}

- (void)releaseDownloadPath:(const std::string&)path {
  _downloadPaths->Release(path);
}

- (BRBrowserWindowController*)openPopupWindowWithContentSize:(NSSize)size {
  BRBrowserWindowController* controller =
      [[BRBrowserWindowController alloc] initWithOwner:self
                                      popupContentSize:size];
  [_windows addObject:controller];
  [controller showWindow:nil];
  return controller;
}

- (void)openNewWindow {
  [self newWindow:nil];
}

- (IBAction)newWindow:(nullable id)sender {
  if (_quitting || !_profilesLoaded) {
    return;
  }
  [[self openWindow] openTabWithURL:nil activate:YES];
}

// Cmd+T with no window open opens a window, as in Chrome.
- (IBAction)newTab:(nullable id)sender {
  [self newWindow:sender];
}

- (void)tryToTerminateApplication {
  if (_quitting) {
    return;
  }
  _quitting = YES;
  // The windows open now are the session to continue with (015).
  [_sessionTimer invalidate];
  _sessionTimer = nil;
  [self saveSessionWithCleanExit:YES];
  _sessionFrozen = YES;
  [_bookmarks saveNow];
  [_profileManager closePage];
  [_settingsWindow closePage];
  for (BRBrowserWindowController* controller in [_windows copy]) {
    [controller closeAllTabs];
  }
  [self quitIfDone];
}

- (void)releaseBrowserObjects {
  [_bookmarks saveNow];
  _adblock.reset();
  _history.reset();  // Finishes queued history work, closes the databases.
  _settingsWatcher = nil;
  _profileManager = nil;
  _settingsWindow = nil;
  [_windows removeAllObjects];
  _requestContexts.clear();
  _internalContext = nullptr;
  _devToolsPreferences.reset();  // Holds CEF values.
}

- (void)quitIfDone {
  if (_quitting && _browserCount <= 0 && _windows.count == 0 &&
      !_profileManager && !_settingsWindow) {
    CefQuitMessageLoop();
  }
}

- (BOOL)applicationShouldHandleReopen:(NSApplication*)app
                    hasVisibleWindows:(BOOL)hasVisibleWindows {
  if (!hasVisibleWindows) {
    [self newWindow:nil];
  }
  return NO;
}

- (BOOL)applicationSupportsSecureRestorableState:(NSApplication*)app {
  return YES;
}

#pragma mark - Profiles

// NSMenuDelegate: the Profiles menu lists the current profiles. A profile
// opens a new tab in the front window; with Option, a new window.
- (void)menuNeedsUpdate:(NSMenu*)menu {
  if (menu == _recentlyClosedMenu) {
    [self fillRecentlyClosedMenu:menu];
    return;
  }
  if (menu == _recentlyVisitedMenu) {
    [self fillRecentlyVisitedMenu:menu];
    return;
  }
  if (menu == _bookmarksMenu) {
    [self fillBookmarksMenu:menu];
    return;
  }
  [menu removeAllItems];
  const std::string& default_id = _profiles->default_profile().id;
  for (const brouk::ProfileInfo& profile : _profiles->profiles()) {
    NSString* name = @(profile.name.c_str());
    NSString* title = profile.id == default_id
                          ? [NSString stringWithFormat:@"%@ (default)", name]
                          : name;
    NSMenuItem* tab = [menu addItemWithTitle:title
                                      action:@selector(newTabInProfile:)
                               keyEquivalent:@""];
    tab.representedObject = @(profile.id.c_str());
    tab.image = brouk::mac::ProfileDot(profile.color);
    tab.toolTip = [NSString stringWithFormat:@"New tab in %@", name];
    NSMenuItem* window = [menu
        addItemWithTitle:[NSString stringWithFormat:@"New Window in %@", name]
                  action:@selector(newWindowInProfile:)
           keyEquivalent:@""];
    window.representedObject = tab.representedObject;
    window.image = tab.image;
    window.alternate = YES;
    window.keyEquivalentModifierMask = NSEventModifierFlagOption;
  }
  [menu addItem:NSMenuItem.separatorItem];
  [menu addItemWithTitle:@"New Profile…"
                  action:@selector(newProfile:)
           keyEquivalent:@""];
  NSMenuItem* manage = [menu addItemWithTitle:@"Manage Profiles…"
                                       action:@selector(manageProfiles:)
                                keyEquivalent:@""];
  manage.identifier = @"manage_profiles";
  brouk::mac::ApplyShortcutsToMenu(menu);
}

// NSMenuDelegate: the Profiles menu is filled only when opened, so the
// shortcut of "Manage Profiles…" (003) is checked here.
- (BOOL)menuHasKeyEquivalent:(NSMenu*)menu
                    forEvent:(NSEvent*)event
                      target:(id _Nullable*)target
                      action:(SEL _Nullable*)action {
  if (menu != _profilesMenu) {
    return NO;
  }
  for (const brouk::Shortcut& shortcut :
       brouk::mac::ShortcutsFor("manage_profiles")) {
    if (brouk::mac::EventMatchesShortcut(event, shortcut)) {
      *target = self;
      *action = @selector(manageProfiles:);
      return YES;
    }
  }
  return NO;
}

#pragma mark - Settings (004)

- (brouk::TabClientDelegate::SearchEngine)searchEngine {
  const brouk::Settings& global = _profiles->global_settings();
  return {brouk::SearchEngineName(global), brouk::SearchTemplate(global)};
}

- (BOOL)askWhereToSaveDownloads {
  return _profiles->global_settings().GetBool("downloads.ask_where_to_save");
}

- (BOOL)jsonViewerEnabled {
  return _profiles->global_settings().GetBool("content.json_viewer");
}

#pragma mark - Developer tools (006)

- (NSString*)dockPosition {
  return @(
      _profiles->global_settings().GetString("devtools.dock_position").c_str());
}

- (CGFloat)dockSize {
  return static_cast<CGFloat>(
      _profiles->global_settings().GetInt("devtools.dock_size"));
}

// The dock's position and size are shared by all tabs and remembered
// (006): stored as settings, which apply them to every window.
- (void)setDockPosition:(NSString*)position {
  [self setGlobalSetting:"devtools.dock_position"
                   value:std::string(position.UTF8String)];
}

- (void)setDockSize:(CGFloat)size {
  [self setGlobalSetting:"devtools.dock_size"
                   value:static_cast<std::int64_t>(std::lround(size))];
}

// The DevTools frontend runs with the internal pages (in memory).
- (CefRefPtr<CefRequestContext>)devToolsContext {
  return [self internalContext];
}

// Request rules (006), per profile, in profiles/<id>/request-rules.yaml.
- (std::filesystem::path)requestRulesPathForProfile:
    (const brouk::ProfileId&)profile {
  return std::filesystem::path(brouk::mac::ProfileDirectory(@(profile.c_str()))
                                   .fileSystemRepresentation) /
         "request-rules.yaml";
}

- (brouk::ToolsHost::Rules)requestRulesForProfile:
    (const brouk::ProfileId&)profile {
  brouk::ToolsHost::Rules rules;
  const brouk::ProfileInfo* info = _profiles->Find(profile);
  if (!info || !brouk::IsValidProfileId(profile)) {
    return rules;
  }
  rules.profile_name = info->name;
  rules.set = brouk::LoadRequestRules([self requestRulesPathForProfile:profile],
                                      rules.problems);
  return rules;
}

- (std::optional<brouk::Error>)setRequestRules:(brouk::RequestRuleSet)rules
                                    forProfile:
                                        (const brouk::ProfileId&)profile {
  if (!_profiles->Find(profile) || !brouk::IsValidProfileId(profile)) {
    return brouk::Error{"There is no such profile."};
  }
  if (std::optional<brouk::Error> error = brouk::SaveRequestRules(
          [self requestRulesPathForProfile:profile], rules)) {
    return error;
  }
  for (BRBrowserWindowController* controller in _windows) {
    [controller requestRulesChanged:rules forProfile:profile];
  }
  return std::nullopt;
}

// A rules file changed by hand: apply every profile's rules again.
- (void)reloadRequestRules {
  for (const brouk::ProfileInfo& profile : _profiles->profiles()) {
    brouk::ToolsHost::Rules rules = [self requestRulesForProfile:profile.id];
    for (const std::string& problem : rules.problems) {
      NSLog(@"Request rules of %s: %s", profile.name.c_str(), problem.c_str());
    }
    for (BRBrowserWindowController* controller in _windows) {
      [controller requestRulesChanged:rules.set forProfile:profile.id];
    }
  }
}

- (NSString*)secretHeaderNames {
  return @(_profiles->global_settings()
               .GetString("devtools.secret_headers")
               .c_str());
}

- (brouk::DevToolsPreferences*)devToolsPreferences {
  if (!_devToolsPreferences) {
    NSString* path = [brouk::mac::AppDataDirectory()
        stringByAppendingPathComponent:@"devtools-preferences.json"];
    _devToolsPreferences =
        std::make_unique<brouk::DevToolsPreferences>(path.UTF8String);
  }
  return _devToolsPreferences.get();
}

- (int)remoteDebuggingPortSetting {
  const brouk::Settings& global = _profiles->global_settings();
  return global.GetBool("devtools.remote_debugging")
             ? static_cast<int>(global.GetInt("devtools.remote_debugging_port"))
             : 0;
}

// Main thread. Applies the developer tools settings to a window.
- (void)applyDevToolsSettingsTo:(BRBrowserWindowController*)controller {
  const brouk::Settings& global = _profiles->global_settings();
  [controller
      setDevToolsToggleOnLeft:global.GetString("devtools.toggle_position") ==
                              "left"];
  [controller setDockPosition:[self dockPosition] size:[self dockSize]];
  controller.remoteDebuggingPort = _remoteDebuggingPort;
  controller.bookmarksBarAlwaysShown =
      global.GetString("bookmarks.bar") == "always";
}

// Main thread. Applies the global settings that act right away: theme,
// shortcuts and spell checking (search and downloads are read when used).
- (void)applySettings {
  const brouk::Settings& global = _profiles->global_settings();
  // Conflicts are reported with the other settings problems (registry).
  std::vector<std::string> ignored;
  brouk::mac::SetShortcuts(brouk::ResolveShortcuts(global, ignored));
  if (_history) {
    const std::string& keep = global.GetString("history.keep");
    _history->SetNeverRecord(
        brouk::ParseUrlPatternList(global.GetString("history.never_record"))
            .value_or(std::vector<brouk::UrlPattern>{}));
    _history->SetRetentionDays(
        keep == "forever" ? std::nullopt
                          : std::optional<int>(std::atoi(keep.c_str())));
  }
  const std::string& theme = global.GetString("appearance.theme");
  NSApp.appearance =
      theme == "light" ? [NSAppearance appearanceNamed:NSAppearanceNameAqua]
      : theme == "dark"
          ? [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua]
          : nil;
  for (const auto& [id, context] : _requestContexts) {
    [self applySpellcheckTo:context];
  }
  BOOL jsonViewer = [self jsonViewerEnabled];
  for (BRBrowserWindowController* controller in _windows) {
    [controller sendRendererConfigWithJsonViewer:jsonViewer];
  }
  [self configureAdblock];
  NSUInteger problems = _profiles->problems().size();
  for (BRBrowserWindowController* controller in _windows) {
    controller.settingsProblemCount = problems;
    [self applyDevToolsSettingsTo:controller];
  }
}

// UI thread. Chromium's own preference (spellcheck::prefs::kSpellCheckEnable).
- (void)applySpellcheckTo:(const CefRefPtr<CefRequestContext>&)context {
  CEF_REQUIRE_UI_THREAD();
  constexpr char kPref[] = "browser.enable_spellchecking";
  if (!context->CanSetPreference(kPref)) {
    return;
  }
  CefRefPtr<CefValue> value = CefValue::Create();
  value->SetBool(_profiles->global_settings().GetBool("content.spellcheck"));
  CefString error;
  context->SetPreference(kPref, value, error);
}

- (std::optional<brouk::Error>)setGlobalSetting:(std::string_view)key
                                          value:(brouk::SettingValue)value {
  std::optional<brouk::Error> error =
      _profiles->SetGlobalSetting(key, std::move(value));
  [self applySettings];
  [self profilesChangedInApp];
  return error;
}

- (std::optional<brouk::Error>)setProfileSetting:(std::string_view)key
                                         profile:(std::string_view)profile
                                           value:(brouk::SettingValue)value {
  std::optional<brouk::Error> error =
      _profiles->SetProfileSetting(profile, key, std::move(value));
  [self profilesChangedInApp];
  return error;
}

- (void)revealSettingsFolder {
  NSString* file = [brouk::mac::AppDataDirectory()
      stringByAppendingPathComponent:@"settings.yaml"];
  if (![NSFileManager.defaultManager fileExistsAtPath:file]) {
    // Write the defaults, so there is a file to edit.
    _profiles->ReplaceGlobalSettings(_profiles->global_settings());
  }
  [NSWorkspace.sharedWorkspace
      activateFileViewerSelectingURLs:@[ [NSURL fileURLWithPath:file] ]];
}

// Presents `panel` as a sheet of the settings window if it is open.
- (void)runPanel:(NSSavePanel*)panel
      completion:(void (^)(NSModalResponse))completion {
  NSWindow* parent = _settingsWindow.window;
  if (parent.visible) {
    [panel beginSheetModalForWindow:parent completionHandler:completion];
  } else {
    completion([panel runModal]);
  }
}

- (void)exportSettingsWithDone:(brouk::SettingsPageDelegate::FileDone)done {
  NSString* extension = @(std::string(brouk::kSettingsExportExtension).c_str());
  NSSavePanel* panel = [NSSavePanel savePanel];
  panel.title = @"Export Settings";
  panel.message = @"Settings for all profiles. Profiles are exported in the "
                  @"profile manager.";
  panel.nameFieldStringValue =
      [@"Brouk settings" stringByAppendingPathExtension:extension];
  if (UTType* type = [UTType typeWithFilenameExtension:extension]) {
    panel.allowedContentTypes = @[ type ];
  }
  std::string exportedAt =
      [NSISO8601DateFormatter
          stringFromDate:[NSDate date]
                timeZone:NSTimeZone.localTimeZone
           formatOptions:NSISO8601DateFormatWithInternetDateTime]
          .UTF8String;
  std::string text =
      brouk::ExportSettings(_profiles->global_settings(), exportedAt);
  __block brouk::SettingsPageDelegate::FileDone answer = std::move(done);
  [self runPanel:panel
      completion:^(NSModalResponse response) {
        if (response != NSModalResponseOK || !panel.URL) {
          answer(false, {}, std::nullopt);
          return;
        }
        NSData* data = [NSData dataWithBytes:text.data() length:text.size()];
        NSError* error = nil;
        if (![data writeToURL:panel.URL
                      options:NSDataWritingAtomic
                        error:&error]) {
          answer(false, {},
                 brouk::Error{std::string("The file could not be saved: ") +
                              error.localizedDescription.UTF8String});
          return;
        }
        answer(true, {}, std::nullopt);
      }];
}

- (void)importSettingsWithDone:(brouk::SettingsPageDelegate::FileDone)done {
  NSString* extension = @(std::string(brouk::kSettingsExportExtension).c_str());
  NSOpenPanel* panel = [NSOpenPanel openPanel];
  panel.title = @"Import Settings";
  panel.message = @"Choose settings exported from Brouk. They replace your "
                  @"current settings for all profiles.";
  panel.canChooseDirectories = NO;
  panel.allowsMultipleSelection = NO;
  if (UTType* type = [UTType typeWithFilenameExtension:extension]) {
    panel.allowedContentTypes = @[ type ];
  }
  __block brouk::SettingsPageDelegate::FileDone answer = std::move(done);
  __weak BRAppDelegate* weakSelf = self;
  [self runPanel:panel
      completion:^(NSModalResponse response) {
        BRAppDelegate* strongSelf = weakSelf;
        if (!strongSelf || response != NSModalResponseOK || !panel.URL) {
          answer(false, {}, std::nullopt);
          return;
        }
        NSNumber* size = nil;
        [panel.URL getResourceValue:&size forKey:NSURLFileSizeKey error:nil];
        NSData* data = size.unsignedLongLongValue <= 1024 * 1024
                           ? [NSData dataWithContentsOfURL:panel.URL]
                           : nil;
        if (!data) {
          answer(false, {},
                 brouk::Error{"This file is not a Brouk settings export."});
          return;
        }
        brouk::Result<brouk::ImportedSettings> parsed =
            brouk::ParseSettingsExport(std::string_view(
                static_cast<const char*>(data.bytes), data.length));
        if (!parsed) {
          answer(false, {}, parsed.error());
          return;
        }
        if (std::optional<brouk::Error> error =
                strongSelf->_profiles->ReplaceGlobalSettings(
                    parsed.value().settings)) {
          answer(false, {}, error);
          return;
        }
        [strongSelf applySettings];
        [strongSelf profilesChangedInApp];
        answer(true, parsed.value().problems, std::nullopt);
      }];
}

#pragma mark - Internal pages (profile manager, settings)

// UI thread. The request context of internal pages (in memory only).
- (CefRefPtr<CefRequestContext>)internalContext {
  if (!_internalContext) {
    CefRequestContextSettings settings;  // No cache path: in memory only.
    _internalContext = CefRequestContext::CreateContext(settings, nullptr);
    NSString* web = [NSBundle.mainBundle.resourcePath
        stringByAppendingPathComponent:@"web"];
    brouk::InstallInternalPages(_internalContext, web.UTF8String);
  }
  return _internalContext;
}

- (IBAction)manageProfiles:(nullable id)sender {
  CEF_REQUIRE_UI_THREAD();
  if (_quitting || !_profilesLoaded) {
    return;
  }
  if (_profileManager) {
    [_profileManager showWindow:nil];
    return;
  }
  _profileManager = [[BRInternalPageWindowController alloc]
      initWithOwner:self
                url:@"brouk://profiles/"
              title:@"Profiles"
               size:NSMakeSize(640, 600)
            handler:std::make_unique<brouk::ProfileManagerHandler>(
                        _profileManagerDelegate.get())
            context:[self internalContext]];
  [_profileManager showWindow:nil];
}

- (nullable brouk::HistoryService*)historyService {
  return _history.get();
}

- (std::vector<brouk::ProfileInfo>)allProfiles {
  return _profiles->profiles();
}

- (std::vector<std::string>)keepListOfProfile:(const brouk::ProfileId&)profile {
  std::vector<std::string> hosts;
  std::optional<brouk::Settings> settings = _profiles->ProfileSettings(profile);
  if (!settings) {
    return hosts;
  }
  for (const brouk::UrlPattern& pattern :
       brouk::ParseUrlPatternList(settings->GetString("history.keep_list"))
           .value_or(std::vector<brouk::UrlPattern>{})) {
    hosts.push_back(pattern.host);
  }
  return hosts;
}

#pragma mark - Clear browsing data (013)

// History → Clear Browsing Data… and the settings page: the dialog on the
// history page of the front window.
- (IBAction)clearBrowsingData:(nullable id)sender {
  BRBrowserWindowController* front =
      [self frontController] ?: [self openWindow];
  [front showHistoryWithFragment:@"clear"];
  [front showWindow:nil];
}

// Size of a folder and everything in it (on a background queue: the cache
// can hold many files).
static void FolderSize(NSString* path, void (^done)(std::int64_t bytes)) {
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    std::int64_t total = 0;
    NSDirectoryEnumerator* files = [NSFileManager.defaultManager
                   enumeratorAtURL:[NSURL fileURLWithPath:path]
        includingPropertiesForKeys:@[ NSURLTotalFileAllocatedSizeKey ]
                           options:0
                      errorHandler:nil];
    for (NSURL* file in files) {
      NSNumber* size = nil;
      [file getResourceValue:&size
                      forKey:NSURLTotalFileAllocatedSizeKey
                       error:nil];
      total += size.longLongValue;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
      done(total);
    });
  });
}

// Cookies are read and deleted with a browser of the profile running: a
// profile's network context (and its cookies) exists only then.
- (void)previewDataOfProfile:(const brouk::ProfileId&)profile
                       since:(std::int64_t)since
                        done:(brouk::HistoryPageDelegate::DataPreviewDone)done {
  CEF_REQUIRE_UI_THREAD();
  NSString* cache = [brouk::mac::ProfileWebDataDirectory(@(profile.c_str()))
      stringByAppendingPathComponent:@"Cache"];
  auto shared = std::make_shared<brouk::HistoryPageDelegate::DataPreviewDone>(
      std::move(done));
  CefRefPtr<CefRequestContext> context =
      [self requestContextForProfile:profile];
  [self withBrowserInProfile:profile
                          do:^(CefRefPtr<CefBrowser> /*browser*/,
                               void (^release)(void)) {
                            brouk::VisitCookiesSince(
                                context, since, /*remove=*/false,
                                [cache, shared, release](
                                    int cookies,
                                    std::set<std::string> /*hosts*/) {
                                  release();
                                  FolderSize(cache, ^(std::int64_t bytes) {
                                    (*shared)(cookies, bytes);
                                  });
                                });
                          }];
}

- (void)clearDataOfProfile:(const brouk::ProfileId&)profile
                     since:(std::int64_t)since
                   cookies:(bool)cookies
                     cache:(bool)cache
                     sites:(std::set<std::string>)sites
                      done:(std::function<void(bool)>)done {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefRequestContext> context =
      [self requestContextForProfile:profile];
  auto finish = std::make_shared<std::function<void(bool)>>(std::move(done));
  auto cookies_ok = std::make_shared<bool>(true);
  // The cache last (it does not depend on the rest).
  auto clear_cache = [context, cache, finish, cookies_ok] {
    if (cache) {
      brouk::ClearHttpCache(context,
                            [finish, cookies_ok] { (*finish)(*cookies_ok); });
    } else {
      (*finish)(*cookies_ok);
    }
  };
  if (!cookies) {
    clear_cache();
    return;
  }
  auto shared_sites = std::make_shared<std::set<std::string>>(std::move(sites));
  [self
      withBrowserInProfile:profile
                        do:^(CefRefPtr<CefBrowser> browser,
                             void (^release)(void)) {
                          brouk::VisitCookiesSince(
                              context, since, /*remove=*/true,
                              [browser, release, shared_sites, clear_cache,
                               cookies_ok](int count,
                                           std::set<std::string> cookie_hosts) {
                                *cookies_ok = count >= 0;
                                shared_sites->insert(cookie_hosts.begin(),
                                                     cookie_hosts.end());
                                brouk::ClearSiteData(
                                    browser,
                                    brouk::OriginsOfHosts(*shared_sites),
                                    [release, clear_cache] {
                                      release();
                                      clear_cache();
                                    });
                              });
                        }];
}

// A browser of `profile` for the DevTools protocol: one of its tabs, or a
// hidden helper browser closed again by `release`.
- (void)withBrowserInProfile:(const brouk::ProfileId&)profile
                          do:(void (^)(CefRefPtr<CefBrowser> browser,
                                       void (^release)(void)))work {
  for (BRBrowserWindowController* controller in _windows) {
    CefRefPtr<CefBrowser> browser = [controller anyBrowserInProfile:profile];
    if (browser) {
      work(browser, ^{
           });
      return;
    }
  }
  NSWindow* hidden =
      [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 200, 200)
                                  styleMask:NSWindowStyleMaskBorderless
                                    backing:NSBackingStoreBuffered
                                      defer:NO];
  hidden.releasedWhenClosed = NO;
  __weak BRAppDelegate* weakSelf = self;
  CefRefPtr<CefClient> client = brouk::MakeHelperBrowserClient(
      [weakSelf, work](CefRefPtr<CefBrowser> browser) {
        BRAppDelegate* strongSelf = weakSelf;
        if (strongSelf) {
          ++strongSelf->_browserCount;
        }
        work(browser, ^{
          browser->GetHost()->CloseBrowser(true);
        });
      },
      // The window stays alive with the helper's client, until it closes.
      [weakSelf, hidden]() {
        [hidden close];
        BRAppDelegate* strongSelf = weakSelf;
        if (strongSelf) {
          --strongSelf->_browserCount;
          [strongSelf quitIfDone];
        }
      });
  CefWindowInfo info;
  info.SetAsChild((__bridge void*)hidden.contentView, CefRect(0, 0, 200, 200));
  info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  CefBrowserSettings settings;
  CefBrowserHost::CreateBrowser(info, client, "about:blank", settings, nullptr,
                                [self requestContextForProfile:profile]);
}

- (std::string)historyView {
  return _profiles->global_settings().GetString("history.view");
}

- (CefRefPtr<CefRequestContext>)internalPagesContext {
  return [self internalContext];
}

- (std::unique_ptr<CefMessageRouterBrowserSide::Handler>)
    internalPageHandler:(NSString*)page
                  tabId:(brouk::TabId)tabId
             controller:(BRBrowserWindowController*)controller {
  if ([page isEqualToString:@"history"]) {
    return std::make_unique<brouk::HistoryPageHandler>(
        std::make_unique<brouk::AppHistoryPage>(self, controller, tabId));
  }
  if ([page isEqualToString:@"bookmarks"]) {
    return std::make_unique<brouk::BookmarksPageHandler>(
        std::make_unique<brouk::AppBookmarksPage>(self, _bookmarks, controller,
                                                  tabId));
  }
  return nullptr;
}

#pragma mark - History menu (013)

- (void)historyChangedForProfile:(const brouk::ProfileId&)profile {
  [self fetchRecentPagesForProfile:profile];
}

- (void)fetchRecentPagesForProfile:(const brouk::ProfileId&)profile {
  if (!_history) {
    return;
  }
  __weak BRAppDelegate* weakSelf = self;
  brouk::ProfileId target = profile;
  _history->RecentPages(
      profile, 10, [weakSelf, target](std::vector<brouk::HistoryMatch> pages) {
        BRAppDelegate* strongSelf = weakSelf;
        if (strongSelf) {
          strongSelf->_recentPages[target] = std::move(pages);
        }
      });
}

- (void)browserWindow:(BRBrowserWindowController*)controller
            closedTab:(brouk::ClosedTab)tab {
  if (!_quitting) {
    _closedTabs.AddTab(std::move(tab));
  }
}

- (void)browserWindow:(BRBrowserWindowController*)controller
    closedWindowWithTabs:(std::vector<brouk::ClosedTab>)tabs {
  if (!_quitting) {
    _closedTabs.AddWindow(controller.windowId, std::move(tabs));
  }
}

// ⇧⌘T: the newest closed tab or window.
- (IBAction)reopenClosedTab:(nullable id)sender {
  if (std::optional<brouk::ClosedEntry> entry = _closedTabs.TakeNewest()) {
    [self reopenEntry:*entry];
  }
}

- (void)reopenClosedEntry:(NSMenuItem*)sender {
  if (std::optional<brouk::ClosedEntry> entry = _closedTabs.Take(sender.tag)) {
    [self reopenEntry:*entry];
  }
}

// A window comes back as a new window with its tabs; a tab goes back into
// its window if that is still open, else into the front window.
- (void)reopenEntry:(const brouk::ClosedEntry&)entry {
  BRBrowserWindowController* target = nil;
  if (!entry.is_window) {
    for (BRBrowserWindowController* controller in _windows) {
      if (controller.windowId == entry.window_id && !controller.isPopup) {
        target = controller;
      }
    }
    target = target ?: [self frontController];
  }
  if (!target) {
    target = [self openWindow];
  }
  for (std::size_t i = 0; i < entry.tabs.size(); ++i) {
    brouk::ClosedTab tab = entry.tabs[i];
    if (!_profiles->Find(tab.profile)) {
      tab.profile = _profiles->default_profile().id;  // Deleted meanwhile.
    }
    if (entry.is_window) {
      tab.position = i;
    }
    [target restoreClosedTab:tab activate:i == 0];
  }
  [target showWindow:nil];
}

- (void)fillRecentlyClosedMenu:(NSMenu*)menu {
  [menu removeAllItems];
  if (_closedTabs.empty()) {
    [menu addItemWithTitle:@"No Closed Tabs" action:nil keyEquivalent:@""]
        .enabled = NO;
    return;
  }
  for (const brouk::ClosedEntry& entry : _closedTabs.entries()) {
    const brouk::ClosedTab& first = entry.tabs.front();
    NSString* title =
        entry.is_window
            ? [NSString stringWithFormat:@"Window (%lu tabs)",
                                         static_cast<unsigned long>(
                                             entry.tabs.size())]
            : @(first.title.empty() ? first.url.c_str() : first.title.c_str());
    NSMenuItem* item = [menu addItemWithTitle:title ?: @""
                                       action:@selector(reopenClosedEntry:)
                                keyEquivalent:@""];
    item.target = self;
    item.tag = static_cast<NSInteger>(entry.id);
    const brouk::ProfileInfo* profile = _profiles->Find(first.profile);
    item.image = brouk::mac::ProfileDot(profile ? profile->color : "grey");
    item.toolTip = @(first.url.c_str());
  }
}

- (void)fillRecentlyVisitedMenu:(NSMenu*)menu {
  [menu removeAllItems];
  BRBrowserWindowController* front = [self frontController];
  brouk::ProfileId profile =
      front ? [front activeProfileId] : _profiles->default_profile().id;
  auto it = _recentPages.find(profile);
  if (it == _recentPages.end()) {
    // Read from the history in the background; shown next time.
    [self fetchRecentPagesForProfile:profile];
    [menu addItemWithTitle:@"Loading…" action:nil keyEquivalent:@""].enabled =
        NO;
    return;
  }
  if (it->second.empty()) {
    [menu addItemWithTitle:@"No History" action:nil keyEquivalent:@""].enabled =
        NO;
    return;
  }
  for (const brouk::HistoryMatch& page : it->second) {
    NSString* title =
        @(page.title.empty() ? page.url.c_str() : page.title.c_str()) ?: @"";
    NSMenuItem* item = [menu addItemWithTitle:title
                                       action:@selector(openRecentPage:)
                                keyEquivalent:@""];
    item.target = self;
    item.representedObject =
        @[ @(page.url.c_str()) ?: @"", @(profile.c_str()) ];
    item.toolTip = @(page.url.c_str());
  }
}

// A recently visited page opens in a new tab of its profile in the front
// window, as Chrome opens History menu items.
- (void)openRecentPage:(NSMenuItem*)sender {
  NSArray<NSString*>* info = sender.representedObject;
  if (![info isKindOfClass:NSArray.class] || info.count != 2) {
    return;
  }
  BRBrowserWindowController* front =
      [self frontController] ?: [self openWindow];
  [front openTabWithURL:info[0] profile:info[1].UTF8String activate:YES];
  [front showWindow:nil];
}

#pragma mark - Bookmarks (011)

- (BRBookmarkStore*)bookmarkStore {
  return _bookmarks;
}

- (std::vector<brouk::ProfileInfo>)profileList {
  return _profiles->profiles();
}

- (IBAction)toggleBookmarksBar:(nullable id)sender {
  BOOL always =
      _profiles->global_settings().GetString("bookmarks.bar") == "always";
  [self setGlobalSetting:"bookmarks.bar"
                   value:brouk::SettingValue(
                             std::string(always ? "new_tab_page" : "always"))];
}

// The bar's bookmarks, then Other bookmarks as a submenu; only what is
// visible in the front window's active profile.
- (void)fillBookmarksMenu:(NSMenu*)menu {
  while (menu.numberOfItems > _bookmarksMenuFixedItems) {
    [menu removeItemAtIndex:menu.numberOfItems - 1];
  }
  BRBrowserWindowController* front = [self frontController];
  brouk::ProfileId profile =
      front ? [front activeProfileId] : _profiles->default_profile().id;
  [self addBookmarksOf:brouk::BookmarkModel::kBarId
                toMenu:menu
               profile:profile];
  const brouk::BookmarkModel& model = [_bookmarks model];
  if (!model.Find(brouk::BookmarkModel::kOtherId)->children.empty()) {
    [menu addItem:NSMenuItem.separatorItem];
    NSMenu* other = [[NSMenu alloc] initWithTitle:@"Other Bookmarks"];
    [self addBookmarksOf:brouk::BookmarkModel::kOtherId
                  toMenu:other
                 profile:profile];
    NSMenuItem* item = [menu addItemWithTitle:@"Other Bookmarks"
                                       action:nil
                                keyEquivalent:@""];
    item.submenu = other;
    item.image = [NSImage imageWithSystemSymbolName:@"folder"
                           accessibilityDescription:nil];
  }
}

- (void)addBookmarksOf:(brouk::BookmarkId)folderId
                toMenu:(NSMenu*)menu
               profile:(const brouk::ProfileId&)profile {
  const brouk::BookmarkModel& model = [_bookmarks model];
  const brouk::BookmarkItem* folder = model.Find(folderId);
  bool any = false;
  for (brouk::BookmarkId id : folder->children) {
    const brouk::BookmarkItem* item = model.Find(id);
    if (item == nullptr || !model.IsVisibleIn(id, profile)) {
      continue;
    }
    any = true;
    NSString* title = @(item->name.c_str()) ?: @"";
    if (item->folder) {
      NSMenu* submenu = [[NSMenu alloc] initWithTitle:title];
      [self addBookmarksOf:id toMenu:submenu profile:profile];
      NSMenuItem* entry = [menu addItemWithTitle:title
                                          action:nil
                                   keyEquivalent:@""];
      entry.submenu = submenu;
      entry.image = [NSImage imageWithSystemSymbolName:@"folder"
                              accessibilityDescription:nil];
    } else {
      NSString* url = @(item->url.c_str()) ?: @"";
      NSMenuItem* entry =
          [menu addItemWithTitle:title.length ? title : url
                          action:@selector(openBookmarkFromMenu:)
                   keyEquivalent:@""];
      entry.target = self;
      entry.tag = static_cast<NSInteger>(id);
      entry.toolTip = url;
    }
  }
  if (!any && folderId != brouk::BookmarkModel::kBarId) {
    [menu addItemWithTitle:@"(Empty)" action:nil keyEquivalent:@""].enabled =
        NO;
  }
}

// In the current tab, as Chrome opens Bookmarks menu items; with ⌘, in a
// new tab.
- (void)openBookmarkFromMenu:(NSMenuItem*)sender {
  BRBrowserWindowController* front =
      [self frontController] ?: [self openWindow];
  BOOL newTab =
      (NSApp.currentEvent.modifierFlags & NSEventModifierFlagCommand) != 0;
  [front openBookmark:static_cast<brouk::BookmarkId>(sender.tag)
          disposition:newTab ? BRBookmarkOpenBackgroundTab
                             : BRBookmarkOpenCurrentTab];
  [front showWindow:nil];
}

- (BOOL)validateMenuItem:(NSMenuItem*)item {
  if (item.action == @selector(toggleBookmarksBar:)) {
    item.state =
        _profiles->global_settings().GetString("bookmarks.bar") == "always"
            ? NSControlStateValueOn
            : NSControlStateValueOff;
    return YES;
  }
  if (item.action == @selector(reopenClosedTab:)) {
    return !_closedTabs.empty();
  }
  return YES;
}

- (BOOL)bookmarkSuggestionsEnabled {
  return _profiles->global_settings().GetBool("suggestions.bookmarks");
}

- (BOOL)historySuggestionsEnabled {
  return _profiles->global_settings().GetBool("suggestions.history");
}

- (std::string)searchSuggestTemplate {
  return brouk::SearchSuggestTemplate(_profiles->global_settings());
}

- (NSArray<BRBrowserWindowController*>*)browserWindowControllers {
  // Front window first.
  NSMutableArray<BRBrowserWindowController*>* result = [NSMutableArray array];
  for (NSWindow* window in NSApp.orderedWindows) {
    for (BRBrowserWindowController* controller in _windows) {
      if (controller.window == window) {
        [result addObject:controller];
      }
    }
  }
  for (BRBrowserWindowController* controller in _windows) {
    if (![result containsObject:controller]) {
      [result addObject:controller];
    }
  }
  return result;
}

// The command palette (003): the settings page, scrolled to one setting.
- (void)openSettingsAtKey:(NSString*)key profile:(nullable NSString*)profileId {
  NSString* scope = profileId ?: @"global";
  [self openSettingsWithFragment:[NSString
                                     stringWithFormat:@"%@:%@", scope, key]];
}

// Settings (004): Cmd+, as in every Mac app.
- (IBAction)openSettings:(nullable id)sender {
  [self openSettingsWithFragment:nil];
}

// `fragment` ("<scope>:<key>") makes the page show that setting.
- (void)openSettingsWithFragment:(nullable NSString*)fragment {
  CEF_REQUIRE_UI_THREAD();
  if (_quitting || !_profilesLoaded) {
    return;
  }
  NSString* url = fragment
                      ? [@"brouk://settings/#" stringByAppendingString:fragment]
                      : @"brouk://settings/";
  if (_settingsWindow) {
    if (fragment) {
      [_settingsWindow loadURL:url];
    }
    [_settingsWindow showWindow:nil];
    return;
  }
  _settingsWindow = [[BRInternalPageWindowController alloc]
      initWithOwner:self
                url:url
              title:@"Settings"
               size:NSMakeSize(720, 680)
            handler:std::make_unique<brouk::SettingsHandler>(
                        _settingsPageDelegate.get())
            context:[self internalContext]];
  [_settingsWindow showWindow:nil];
}

- (void)internalPageDidCreateBrowser {
  ++_browserCount;
}

- (void)internalPageDidClose:(BRInternalPageWindowController*)controller {
  --_browserCount;
  if (_profileManager == controller) {
    _profileManager = nil;
    _profileManagerAtStartup = NO;
  }
  if (_settingsWindow == controller) {
    _settingsWindow = nil;
  }
  [self quitIfDone];
}

- (BOOL)internalPageHandleKeyEvent:(NSEvent*)event {
  return [NSApp.mainMenu performKeyEquivalent:event];
}

// A link in DevTools of an internal page (006): a tab of the default
// profile in the front window, as Chrome opens links from its own pages.
- (void)openURLFromDevTools:(NSString*)url {
  if (_quitting || !_profilesLoaded || ![url isKindOfClass:NSString.class]) {
    return;
  }
  BRBrowserWindowController* front = [self frontController];
  if (!front) {
    [self openWindowWithURL:url profile:_profiles->default_profile().id];
    return;
  }
  [front openTabWithURL:url
                profile:_profiles->default_profile().id
               activate:YES];
  [front showWindow:nil];
}

// The profiles changed here (not through the files): redraw at once.
- (void)profilesChangedInApp {
  for (BRBrowserWindowController* controller in _windows) {
    [controller profilesDidChange];
  }
  [self configureAdblock];
}

// The lists in use (global settings, the system language) and the category
// sets of the profiles that block (007).
- (void)configureAdblock {
  if (!_adblock) {
    return;
  }
  const brouk::Settings& global = _profiles->global_settings();
  auto ids = [](const std::string& text) {
    std::vector<std::string> result;
    std::string current;
    for (char c : text + ",") {
      if (c == ',' || c == ' ') {
        if (!current.empty()) {
          result.push_back(current);
        }
        current.clear();
      } else {
        current += c;
      }
    }
    return result;
  };
  brouk::FilterListChoice choice{
      .turned_on = ids(global.GetString("adblock.lists_on")),
      .turned_off = ids(global.GetString("adblock.lists_off")),
      .custom = brouk::ParseCustomFilterLists(
          global.GetString("adblock.custom_lists"))};
  NSString* language = NSLocale.preferredLanguages.firstObject ?: @"en";
  std::vector<brouk::FilterListDef> lists =
      brouk::ActiveFilterLists(choice, language.UTF8String);
  std::vector<std::set<brouk::FilterCategory>> sets;
  std::string signature;
  for (const brouk::FilterListDef& list : lists) {
    signature += list.id + "\n";
  }
  for (const brouk::ProfileInfo& profile : _profiles->profiles()) {
    std::optional<brouk::Settings> settings =
        _profiles->ProfileSettings(profile.id);
    if (!settings || !settings->GetBool("adblock.enabled")) {
      continue;
    }
    std::set<brouk::FilterCategory> categories =
        brouk::ParseFilterCategories(settings->GetString("adblock.categories"));
    signature += "|" + brouk::FormatFilterCategories(categories);
    sets.push_back(std::move(categories));
  }
  if (signature == _adblockConfiguration) {
    return;
  }
  _adblockConfiguration = signature;
  _adblock->Configure(std::move(lists), std::move(sets));
}

- (nullable brouk::AdblockService*)adblockService {
  return _adblock.get();
}

- (std::vector<brouk::ProfileManagerDelegate::Summary>)profileSummaries {
  std::vector<brouk::ProfileManagerDelegate::Summary> summaries;
  const std::string& default_id = _profiles->default_profile().id;
  for (const brouk::ProfileInfo& profile : _profiles->profiles()) {
    NSUInteger tabs = 0;
    for (BRBrowserWindowController* controller in _windows) {
      tabs += [controller tabCountInProfile:profile.id];
    }
    summaries.push_back(
        {profile, profile.id == default_id, static_cast<int>(tabs)});
  }
  return summaries;
}

- (brouk::Result<brouk::ProfileId>)createProfileNamed:(const std::string&)name
                                                color:
                                                    (const std::string&)color {
  brouk::Result<brouk::ProfileId> created =
      _profiles->CreateProfile(name, color);
  [self profilesChangedInApp];
  return created;
}

- (std::optional<brouk::Error>)updateProfile:(const brouk::ProfileId&)profile
                                        name:(const std::string&)name
                                       color:(const std::string&)color {
  std::optional<brouk::Error> error =
      _profiles->UpdateProfile(profile, name, color);
  [self profilesChangedInApp];
  return error;
}

- (std::optional<brouk::Error>)makeDefaultProfile:
    (const brouk::ProfileId&)profile {
  std::optional<brouk::Error> error = _profiles->SetDefaultProfile(profile);
  [self profilesChangedInApp];
  return error;
}

- (std::optional<brouk::Error>)setShowProfileManagerAtStartup:(BOOL)show {
  return _profiles->SetShowManagerAtStartup(show);
}

- (BOOL)showProfileManagerAtStartup {
  return _profiles->show_manager_at_startup();
}

- (std::optional<brouk::Error>)openProfile:(const brouk::ProfileId&)profile {
  if (!_profiles->Find(profile)) {
    return brouk::Error{"There is no such profile."};
  }
  if (_quitting) {
    return brouk::Error{"Brouk is closing."};
  }
  [[self openWindow] openTabWithURL:nil profile:profile activate:YES];
  // Chrome's profile picker closes once a profile is chosen at startup.
  if (_profileManagerAtStartup) {
    [_profileManager closePage];
  }
  return std::nullopt;
}

// Export (005): the user picks where to save; the file holds the profile's
// settings only (see core/profile_export.h).
- (void)exportProfile:(const brouk::ProfileId&)profile
                 done:(brouk::ProfileManagerDelegate::ExportDone)done {
  std::optional<brouk::Settings> settings = _profiles->ProfileSettings(profile);
  const brouk::ProfileInfo* info = _profiles->Find(profile);
  if (!settings || !info) {
    done(false, brouk::Error{"There is no such profile."});
    return;
  }
  NSString* name = @(info->name.c_str());
  // File names cannot contain "/" or ":" on macOS.
  NSString* fileName =
      [[[name componentsSeparatedByCharactersInSet:
                  [NSCharacterSet characterSetWithCharactersInString:@"/:"]]
          componentsJoinedByString:@"-"]
          stringByAppendingPathExtension:@(std::string(
                                               brouk::kProfileExportExtension)
                                               .c_str())];
  NSSavePanel* panel = [NSSavePanel savePanel];
  panel.title = [NSString stringWithFormat:@"Export “%@”", name];
  panel.message = @"The file contains the profile's name, colour and "
                  @"settings. It contains no sign-ins, cookies or cache.";
  panel.nameFieldStringValue = fileName;
  UTType* type = [UTType
      typeWithFilenameExtension:@(std::string(brouk::kProfileExportExtension)
                                      .c_str())];
  if (type) {
    panel.allowedContentTypes = @[ type ];
  }
  std::string exportedAt =
      [NSISO8601DateFormatter
          stringFromDate:[NSDate date]
                timeZone:NSTimeZone.localTimeZone
           formatOptions:NSISO8601DateFormatWithInternetDateTime]
          .UTF8String;
  // With its linked bookmarks (011).
  std::string text = brouk::ExportProfile(
      *settings, exportedAt,
      brouk::ExportProfileBookmarks([_bookmarks model], profile));
  __block brouk::ProfileManagerDelegate::ExportDone answer = std::move(done);
  void (^completion)(NSModalResponse) = ^(NSModalResponse response) {
    if (response != NSModalResponseOK || !panel.URL) {
      answer(false, std::nullopt);
      return;
    }
    NSData* data = [NSData dataWithBytes:text.data() length:text.size()];
    NSError* error = nil;
    if (![data writeToURL:panel.URL options:NSDataWritingAtomic error:&error]) {
      answer(false, brouk::Error{std::string("The file could not be saved: ") +
                                 error.localizedDescription.UTF8String});
      return;
    }
    answer(true, std::nullopt);
  };
  NSWindow* parent = _profileManager.window;
  if (parent.visible) {
    [panel beginSheetModalForWindow:parent completionHandler:completion];
  } else {
    completion([panel runModal]);
  }
}

// Import (005): the user picks an export file; it becomes a new profile.
- (void)importProfileWithDone:(brouk::ProfileManagerDelegate::ImportDone)done {
  NSOpenPanel* panel = [NSOpenPanel openPanel];
  panel.title = @"Import a Profile";
  panel.message = @"Choose a profile exported from Brouk.";
  panel.canChooseDirectories = NO;
  panel.allowsMultipleSelection = NO;
  UTType* type = [UTType
      typeWithFilenameExtension:@(std::string(brouk::kProfileExportExtension)
                                      .c_str())];
  if (type) {
    panel.allowedContentTypes = @[ type ];
  }
  __block brouk::ProfileManagerDelegate::ImportDone answer = std::move(done);
  __weak BRAppDelegate* weakSelf = self;
  void (^completion)(NSModalResponse) = ^(NSModalResponse response) {
    BRAppDelegate* strongSelf = weakSelf;
    if (!strongSelf || response != NSModalResponseOK || !panel.URL) {
      answer(brouk::ProfileManagerDelegate::ImportResult{});
      return;
    }
    // Export files are small; anything big is not one (the parser has a
    // limit too).
    NSNumber* size = nil;
    [panel.URL getResourceValue:&size forKey:NSURLFileSizeKey error:nil];
    if (size.unsignedLongLongValue > 1024 * 1024) {
      answer(brouk::Error{"This file is not a Brouk profile export."});
      return;
    }
    NSData* data = [NSData dataWithContentsOfURL:panel.URL];
    if (!data) {
      answer(brouk::Error{"The file could not be read."});
      return;
    }
    brouk::Result<brouk::ImportedProfile> parsed = brouk::ParseProfileExport(
        std::string_view(static_cast<const char*>(data.bytes), data.length));
    if (!parsed) {
      answer(parsed.error());
      return;
    }
    brouk::Result<brouk::ProfileId> created =
        strongSelf->_profiles->ImportProfile(parsed.value().settings);
    if (!created) {
      answer(created.error());
      return;
    }
    std::vector<std::string> problems = parsed.value().problems;
    if (parsed.value().bookmarks) {
      const brouk::YamlNode& section = *parsed.value().bookmarks;
      brouk::ProfileId newProfile = created.value();
      [strongSelf->_bookmarks change:[&](brouk::BookmarkModel& model) {
        brouk::ProfileBookmarksImport bookmarks = brouk::ImportProfileBookmarks(
            model, section, newProfile,
            static_cast<std::int64_t>(NSDate.date.timeIntervalSince1970));
        problems.insert(problems.end(), bookmarks.problems.begin(),
                        bookmarks.problems.end());
      }];
    }
    [strongSelf profilesChangedInApp];
    answer(
        brouk::ProfileManagerDelegate::ImportResult{created.value(), problems});
  };
  NSWindow* parent = _profileManager.window;
  if (parent.visible) {
    [panel beginSheetModalForWindow:parent completionHandler:completion];
  } else {
    completion([panel runModal]);
  }
}

// Deleting asks natively, so the page cannot delete without the user (005:
// "after confirmation; if it has open tabs, the confirmation says so").
- (void)deleteProfile:(const brouk::ProfileId&)profile
                 done:(brouk::ProfileManagerDelegate::DeleteDone)done {
  const brouk::ProfileInfo* info = _profiles->Find(profile);
  if (!info) {
    done(false, brouk::Error{"There is no such profile."});
    return;
  }
  if (_profiles->default_profile().id == profile) {
    done(false, brouk::Error{"The default profile cannot be deleted. Make "
                             "another profile the default first."});
    return;
  }
  NSUInteger tabs = 0;
  for (BRBrowserWindowController* controller in _windows) {
    tabs += [controller tabCountInProfile:profile];
  }
  NSAlert* alert = [[NSAlert alloc] init];
  alert.alertStyle = NSAlertStyleWarning;
  alert.messageText = [NSString
      stringWithFormat:@"Delete the profile “%s”?", info->name.c_str()];
  NSString* tabsText =
      tabs == 0 ? @""
                : [NSString stringWithFormat:@" Its %lu open %@ will close.",
                                             static_cast<unsigned long>(tabs),
                                             tabs == 1 ? @"tab" : @"tabs"];
  // Its bookmarks are kept as general ones (011).
  int linked = [_bookmarks model].CountLinkedTo(profile);
  NSString* bookmarksText =
      linked == 0
          ? @""
          : [NSString stringWithFormat:@" %d %@ linked to it will become "
                                       @"general (shown in every profile).",
                                       linked,
                                       linked == 1 ? @"bookmark or folder"
                                                   : @"bookmarks and folders"];
  alert.informativeText = [NSString
      stringWithFormat:@"Its sign-ins, cookies, history and settings are "
                       @"removed from this Mac. This cannot be undone.%@%@",
                       tabsText, bookmarksText];
  [alert addButtonWithTitle:@"Delete"].hasDestructiveAction = YES;
  [alert addButtonWithTitle:@"Cancel"];
  __block brouk::ProfileManagerDelegate::DeleteDone answer = std::move(done);
  brouk::ProfileId target = profile;
  __weak BRAppDelegate* weakSelf = self;
  void (^completion)(NSModalResponse) = ^(NSModalResponse response) {
    BRAppDelegate* strongSelf = weakSelf;
    if (!strongSelf || response != NSAlertFirstButtonReturn) {
      answer(false, std::nullopt);
      return;
    }
    for (BRBrowserWindowController* controller in [strongSelf->_windows copy]) {
      [controller closeTabsInProfile:target];
    }
    strongSelf->_requestContexts.erase(target);
    if (strongSelf->_history) {
      strongSelf->_history->CloseProfile(target);
    }
    strongSelf->_closedTabs.RemoveProfile(target);
    strongSelf->_recentPages.erase(target);
    std::optional<brouk::Error> error =
        strongSelf->_profiles->DeleteProfile(target);
    if (!error) {
      [strongSelf->_bookmarks change:[&](brouk::BookmarkModel& model) {
        model.UnlinkProfile(target);
      }];
    }
    [strongSelf profilesChangedInApp];
    answer(!error, error);
  };
  NSWindow* sheetParent = _profileManager.window;
  if (sheetParent.visible) {
    [alert beginSheetModalForWindow:sheetParent completionHandler:completion];
  } else {
    completion([alert runModal]);
  }
}

- (void)newTabInProfile:(NSMenuItem*)sender {
  NSString* profile = sender.representedObject;
  BRBrowserWindowController* front = [self frontController];
  if (!profile || _quitting || !_profilesLoaded) {
    return;
  }
  if (!front) {
    front = [self openWindow];
  }
  [front openTabWithURL:nil profile:profile.UTF8String activate:YES];
  [front showWindow:nil];
}

- (void)newWindowInProfile:(NSMenuItem*)sender {
  NSString* profile = sender.representedObject;
  if (!profile || _quitting || !_profilesLoaded) {
    return;
  }
  [[self openWindow] openTabWithURL:nil
                            profile:profile.UTF8String
                           activate:YES];
}

// Asks for a name and a colour and creates the profile; then opens a tab in
// it, so its group appears.
- (IBAction)newProfile:(nullable id)sender {
  [self askForNewProfileWithName:@"" color:_profiles->UnusedColor() error:nil];
}

- (void)askForNewProfileWithName:(NSString*)name
                           color:(const std::string&)color
                           error:(nullable NSString*)error {
  NSAlert* alert = [[NSAlert alloc] init];
  alert.messageText = @"New Profile";
  alert.informativeText =
      error
          ?: @"A profile keeps its own sign-ins, cookies and settings. Its "
             @"tabs form their own group in each window.";
  [alert addButtonWithTitle:@"Create"];
  [alert addButtonWithTitle:@"Cancel"];

  NSView* fields = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 260, 60)];
  NSTextField* nameField =
      [[NSTextField alloc] initWithFrame:NSMakeRect(0, 32, 260, 24)];
  nameField.placeholderString = @"Name, e.g. Work or Client A";
  nameField.stringValue = name;
  nameField.accessibilityLabel = @"Profile name";
  [fields addSubview:nameField];
  NSPopUpButton* colorButton =
      [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 160, 26)
                                 pullsDown:NO];
  colorButton.accessibilityLabel = @"Profile colour";
  for (std::string_view colorName : brouk::ProfileColorNames()) {
    NSString* title = @(std::string(colorName).c_str());
    [colorButton addItemWithTitle:title.capitalizedString];
    colorButton.lastItem.representedObject = title;
    colorButton.lastItem.image = brouk::mac::ProfileDot(colorName);
    if (colorName == color) {
      [colorButton selectItem:colorButton.lastItem];
    }
  }
  [fields addSubview:colorButton];
  alert.accessoryView = fields;
  alert.window.initialFirstResponder = nameField;

  if ([alert runModal] != NSAlertFirstButtonReturn) {
    return;
  }
  NSString* newName = [nameField.stringValue
      stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
  std::string newColor =
      [colorButton.selectedItem.representedObject UTF8String] ?: color;
  brouk::Result<brouk::ProfileId> created =
      _profiles->CreateProfile(newName.UTF8String, newColor);
  if (!created) {
    [self askForNewProfileWithName:newName
                             color:newColor
                             error:@(created.error().message.c_str())];
    return;
  }
  BRBrowserWindowController* front = [self frontController];
  if (!front) {
    front = [self openWindow];
  }
  [front openTabWithURL:nil profile:created.value() activate:YES];
  [front showWindow:nil];
}

#pragma mark - Main menu

- (NSMenuItem*)addItem:(NSMenu*)menu
                 title:(NSString*)title
                action:(nullable SEL)action
                   key:(NSString*)key
             modifiers:(NSEventModifierFlags)modifiers {
  NSMenuItem* item = [menu addItemWithTitle:title
                                     action:action
                              keyEquivalent:key];
  item.keyEquivalentModifierMask = modifiers;
  return item;
}

// A browser action whose shortcuts come from the settings (003); the
// identifier is the action id (see menu_shortcuts.h).
- (NSMenuItem*)addItem:(NSMenu*)menu
                 title:(NSString*)title
                action:(SEL)action
              shortcut:(NSString*)actionId {
  NSMenuItem* item = [menu addItemWithTitle:title
                                     action:action
                              keyEquivalent:@""];
  item.identifier = actionId;
  return item;
}

- (NSMenu*)submenu:(NSString*)title in:(NSMenu*)bar {
  NSMenuItem* holder = [bar addItemWithTitle:title
                                      action:nil
                               keyEquivalent:@""];
  NSMenu* menu = [[NSMenu alloc] initWithTitle:title];
  holder.submenu = menu;
  return menu;
}

// Menus and shortcuts follow Chrome on macOS (012, item 19).
- (NSMenu*)buildMainMenu {
  const NSEventModifierFlags cmd = NSEventModifierFlagCommand;
  const NSEventModifierFlags shift = NSEventModifierFlagShift;
  const NSEventModifierFlags opt = NSEventModifierFlagOption;
  NSString* appName = @(brouk::kAppName);

  NSMenu* bar = [[NSMenu alloc] initWithTitle:@""];

  NSMenu* app = [self submenu:appName in:bar];
  [self addItem:app
          title:[@"About " stringByAppendingString:appName]
         action:@selector(orderFrontStandardAboutPanel:)
            key:@""
      modifiers:0];
  [app addItem:NSMenuItem.separatorItem];
  [self addItem:app
          title:@"Settings…"
         action:@selector(openSettings:)
       shortcut:@"settings"];
  [app addItem:NSMenuItem.separatorItem];
  NSMenu* services = [[NSMenu alloc] initWithTitle:@"Services"];
  [app addItemWithTitle:@"Services" action:nil keyEquivalent:@""].submenu =
      services;
  NSApp.servicesMenu = services;
  [app addItem:NSMenuItem.separatorItem];
  [self addItem:app
          title:[@"Hide " stringByAppendingString:appName]
         action:@selector(hide:)
            key:@"h"
      modifiers:cmd];
  [self addItem:app
          title:@"Hide Others"
         action:@selector(hideOtherApplications:)
            key:@"h"
      modifiers:cmd | opt];
  [self addItem:app
          title:@"Show All"
         action:@selector(unhideAllApplications:)
            key:@""
      modifiers:0];
  [app addItem:NSMenuItem.separatorItem];
  [self addItem:app
          title:[@"Quit " stringByAppendingString:appName]
         action:@selector(terminate:)
            key:@"q"
      modifiers:cmd];

  NSMenu* file = [self submenu:@"File" in:bar];
  [self addItem:file
          title:@"New Tab"
         action:@selector(newTab:)
       shortcut:@"new_tab"];
  [self addItem:file
          title:@"New Window"
         action:@selector(newWindow:)
       shortcut:@"new_window"];
  [self addItem:file
          title:@"Open Location…"
         action:@selector(focusLocation:)
       shortcut:@"open_location"];
  [file addItem:NSMenuItem.separatorItem];
  [self addItem:file
          title:@"Print…"
         action:@selector(printPage:)
       shortcut:@"print"];
  [file addItem:NSMenuItem.separatorItem];
  [self addItem:file
          title:@"Close Window"
         action:@selector(performClose:)
       shortcut:@"close_window"];
  [self addItem:file
          title:@"Close Tab"
         action:@selector(closeCurrentTab:)
       shortcut:@"close_tab"];

  NSMenu* edit = [self submenu:@"Edit" in:bar];
  [self addItem:edit
          title:@"Undo"
         action:@selector(undo:)
            key:@"z"
      modifiers:cmd];
  [self addItem:edit
          title:@"Redo"
         action:@selector(redo:)
            key:@"z"
      modifiers:cmd | shift];
  [edit addItem:NSMenuItem.separatorItem];
  [self addItem:edit
          title:@"Cut"
         action:@selector(cut:)
            key:@"x"
      modifiers:cmd];
  [self addItem:edit
          title:@"Copy"
         action:@selector(copy:)
            key:@"c"
      modifiers:cmd];
  [self addItem:edit
          title:@"Paste"
         action:@selector(paste:)
            key:@"v"
      modifiers:cmd];
  [self addItem:edit
          title:@"Paste and Match Style"
         action:@selector(pasteAndMatchStyle:)
            key:@"v"
      modifiers:cmd | shift | opt];
  [self addItem:edit
          title:@"Delete"
         action:@selector(delete:)
            key:@""
      modifiers:0];
  [self addItem:edit
          title:@"Select All"
         action:@selector(selectAll:)
            key:@"a"
      modifiers:cmd];
  [edit addItem:NSMenuItem.separatorItem];
  NSMenu* find = [[NSMenu alloc] initWithTitle:@"Find"];
  [edit addItemWithTitle:@"Find" action:nil keyEquivalent:@""].submenu = find;
  [self addItem:find
          title:@"Find…"
         action:@selector(showFindBar:)
       shortcut:@"find"];
  [self addItem:find
          title:@"Find Next"
         action:@selector(findNext:)
       shortcut:@"find_next"];
  [self addItem:find
          title:@"Find Previous"
         action:@selector(findPrevious:)
       shortcut:@"find_previous"];

  NSMenu* view = [self submenu:@"View" in:bar];
  [self addItem:view
          title:@"Command Palette…"
         action:@selector(showCommandPalette:)
       shortcut:@"command_palette"];
  [self addItem:view
          title:@"Always Show Bookmarks Bar"
         action:@selector(toggleBookmarksBar:)
       shortcut:@"toggle_bookmarks_bar"];
  [view addItem:NSMenuItem.separatorItem];
  [self addItem:view
          title:@"Stop"
         action:@selector(stopLoading:)
       shortcut:@"stop"];
  [self addItem:view
          title:@"Reload This Page"
         action:@selector(reload:)
       shortcut:@"reload"];
  [self addItem:view
          title:@"Force Reload This Page"
         action:@selector(reloadIgnoringCache:)
       shortcut:@"force_reload"];
  [view addItem:NSMenuItem.separatorItem];
  [self addItem:view
          title:@"Actual Size"
         action:@selector(resetZoom:)
       shortcut:@"zoom_reset"];
  [self addItem:view
          title:@"Zoom In"
         action:@selector(zoomIn:)
       shortcut:@"zoom_in"];
  [self addItem:view
          title:@"Zoom Out"
         action:@selector(zoomOut:)
       shortcut:@"zoom_out"];
  [view addItem:NSMenuItem.separatorItem];
  [self addItem:view
          title:@"Enter Full Screen"
         action:@selector(toggleFullScreen:)
       shortcut:@"full_screen"];

  // View > Developer, as in Chrome (006).
  [view addItem:NSMenuItem.separatorItem];
  NSMenu* developer = [[NSMenu alloc] initWithTitle:@"Developer"];
  [view addItemWithTitle:@"Developer" action:nil keyEquivalent:@""].submenu =
      developer;
  [self addItem:developer
          title:@"Developer Tools"
         action:@selector(toggleDevTools:)
       shortcut:@"devtools"];
  [self addItem:developer
          title:@"JavaScript Console"
         action:@selector(showDevToolsConsole:)
       shortcut:@"devtools_console"];
  [self addItem:developer
          title:@"Inspect Elements"
         action:@selector(pickElementToInspect:)
       shortcut:@"devtools_inspect"];

  NSMenu* history = [self submenu:@"History" in:bar];
  [self addItem:history
          title:@"Back"
         action:@selector(goBack:)
       shortcut:@"back"];
  [self addItem:history
          title:@"Forward"
         action:@selector(goForward:)
       shortcut:@"forward"];
  [self addItem:history
          title:@"Home"
         action:@selector(goHome:)
       shortcut:@"home"];
  [history addItem:NSMenuItem.separatorItem];
  [self addItem:history
          title:@"Show Full History"
         action:@selector(showHistory:)
       shortcut:@"show_history"];
  [self addItem:history
          title:@"Reopen Closed Tab"
         action:@selector(reopenClosedTab:)
       shortcut:@"reopen_closed_tab"];
  [self addItem:history
          title:@"Clear Browsing Data…"
         action:@selector(clearBrowsingData:)
       shortcut:@"clear_browsing_data"];
  // Filled when opened (menuNeedsUpdate:).
  _recentlyClosedMenu = [self submenu:@"Recently Closed" in:history];
  _recentlyClosedMenu.delegate = self;
  _recentlyVisitedMenu = [self submenu:@"Recently Visited" in:history];
  _recentlyVisitedMenu.delegate = self;

  // Bookmarks (011), as in Chrome; the bookmarks are added when it opens.
  NSMenu* bookmarks = [self submenu:@"Bookmarks" in:bar];
  [self addItem:bookmarks
          title:@"Bookmark This Tab…"
         action:@selector(bookmarkPage:)
       shortcut:@"bookmark_page"];
  [self addItem:bookmarks
          title:@"Bookmark All Tabs…"
         action:@selector(bookmarkAllTabs:)
       shortcut:@"bookmark_all_tabs"];
  [self addItem:bookmarks
          title:@"Bookmark Manager"
         action:@selector(showBookmarkManager:)
       shortcut:@"bookmark_manager"];
  [self addItem:bookmarks
          title:@"Always Show Bookmarks Bar"
         action:@selector(toggleBookmarksBar:)
       shortcut:@"toggle_bookmarks_bar"];
  [bookmarks addItem:NSMenuItem.separatorItem];
  _bookmarksMenu = bookmarks;
  _bookmarksMenuFixedItems = bookmarks.numberOfItems;
  bookmarks.delegate = self;

  // Profiles (005), like Chrome's Profiles menu; filled when opened.
  NSMenu* profiles = [self submenu:@"Profiles" in:bar];
  profiles.delegate = self;
  _profilesMenu = profiles;

  // Tab, as in Chrome.
  NSMenu* tab = [self submenu:@"Tab" in:bar];
  [self addItem:tab
          title:@"Select Next Tab"
         action:@selector(selectNextTab:)
       shortcut:@"next_tab"];
  [self addItem:tab
          title:@"Select Previous Tab"
         action:@selector(selectPreviousTab:)
       shortcut:@"previous_tab"];
  // Tabs by number, not shown in the menu (as in Chrome); the tag is the
  // number, 9 being the last tab.
  for (NSInteger number = 1; number <= 9; ++number) {
    NSString* identifier =
        number == 9
            ? @"select_last_tab"
            : [NSString stringWithFormat:@"select_tab_%ld", (long)number];
    NSMenuItem* item = [self
         addItem:tab
           title:[NSString stringWithFormat:@"Select Tab %ld", (long)number]
          action:@selector(selectTabByNumber:)
        shortcut:identifier];
    item.tag = number;
    item.hidden = YES;
    item.allowsKeyEquivalentWhenHidden = YES;
  }
  [tab addItem:NSMenuItem.separatorItem];
  // The title switches to "Unpin Tab" (validateMenuItem:).
  [self addItem:tab
          title:@"New Tab to the Right"
         action:@selector(newTabToTheRight:)
       shortcut:@"new_tab_right"];
  [self addItem:tab
          title:@"Duplicate Tab"
         action:@selector(duplicateTab:)
       shortcut:@"duplicate_tab"];
  [self addItem:tab
          title:@"Pin Tab"
         action:@selector(togglePinTab:)
       shortcut:@"pin_tab"];
  [self addItem:tab
          title:@"Mute Tab"
         action:@selector(toggleMuteTab:)
       shortcut:@"mute_tab"];
  [self addItem:tab
          title:@"Site Information"
         action:@selector(showSiteInfo:)
       shortcut:@"site_info"];
  [tab addItem:NSMenuItem.separatorItem];
  [self addItem:tab
          title:@"Close Other Tabs"
         action:@selector(closeOtherTabs:)
       shortcut:@"close_other_tabs"];
  [self addItem:tab
          title:@"Close Tabs to the Left"
         action:@selector(closeTabsToTheLeft:)
       shortcut:@"close_tabs_left"];
  [self addItem:tab
          title:@"Close Tabs to the Right"
         action:@selector(closeTabsToTheRight:)
       shortcut:@"close_tabs_right"];

  NSMenu* window = [self submenu:@"Window" in:bar];
  [self addItem:window
          title:@"Minimize"
         action:@selector(performMiniaturize:)
            key:@"m"
      modifiers:cmd];
  [self addItem:window
          title:@"Zoom"
         action:@selector(performZoom:)
            key:@""
      modifiers:0];
  [window addItem:NSMenuItem.separatorItem];
  [self addItem:window
          title:@"Bring All to Front"
         action:@selector(arrangeInFront:)
            key:@""
      modifiers:0];
  NSApp.windowsMenu = window;

  return bar;
}

@end
