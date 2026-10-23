#import "platform/mac/app_delegate.h"

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#import "platform/mac/browser_window_controller.h"
#import "platform/mac/data_paths.h"
#import "platform/mac/internal_page_window.h"
#import "platform/mac/profile_colors.h"
#import "platform/mac/settings_watcher.h"

#include "brouk/app_info.h"
#include "cef/browser_app.h"
#include "cef/devtools_preferences.h"
#include "cef/internal_scheme.h"
#include "cef/profile_manager_handler.h"
#include "cef/settings_handler.h"
#include "core/address_input.h"
#include "core/download_paths.h"
#include "core/profile_export.h"
#include "core/profile_registry.h"
#include "core/search_engines.h"
#include "core/settings_export.h"
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/cef_request_context.h"
#include "include/wrapper/cef_helpers.h"

namespace {

// A small filled circle in the profile's colour, for menus.
NSImage* ProfileDot(const std::string& color) {
  NSColor* fill = brouk::mac::ProfileColor(color);
  return [NSImage
       imageWithSize:NSMakeSize(12, 12)
             flipped:NO
      drawingHandler:^BOOL(NSRect rect) {
        [fill setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSInsetRect(rect, 1, 1)] fill];
        return YES;
      }];
}

}  // namespace

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
- (void)exportSettingsWithDone:(brouk::SettingsPageDelegate::FileDone)done;
- (void)importSettingsWithDone:(brouk::SettingsPageDelegate::FileDone)done;
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
  // Our tab strip replaces macOS window tabs.
  NSWindow.allowsAutomaticWindowTabbing = NO;
}

- (void)cefContextInitialized {
  CEF_REQUIRE_UI_THREAD();
  _profilesLoaded = YES;
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
  if (_profiles->show_manager_at_startup() && request.urls.empty() &&
      !request.profile) {
    _profileManagerAtStartup = YES;
    [self manageProfiles:nil];
    return;
  }
  [self openLaunchRequest:request inController:nil];
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
  [_profileManager closePage];
  [_settingsWindow closePage];
  for (BRBrowserWindowController* controller in [_windows copy]) {
    [controller closeAllTabs];
  }
  [self quitIfDone];
}

- (void)releaseBrowserObjects {
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
    tab.image = ProfileDot(profile.color);
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
  [menu addItemWithTitle:@"Manage Profiles…"
                  action:@selector(manageProfiles:)
           keyEquivalent:@""];
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
}

// Main thread. Applies the global settings that act right away: theme and
// spell checking (search and downloads are read when used).
- (void)applySettings {
  const brouk::Settings& global = _profiles->global_settings();
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

// Settings (004): Cmd+, as in every Mac app.
- (IBAction)openSettings:(nullable id)sender {
  CEF_REQUIRE_UI_THREAD();
  if (_quitting || !_profilesLoaded) {
    return;
  }
  if (_settingsWindow) {
    [_settingsWindow showWindow:nil];
    return;
  }
  _settingsWindow = [[BRInternalPageWindowController alloc]
      initWithOwner:self
                url:@"brouk://settings/"
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

// The profiles changed here (not through the files): redraw at once.
- (void)profilesChangedInApp {
  for (BRBrowserWindowController* controller in _windows) {
    [controller profilesDidChange];
  }
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
  std::string text = brouk::ExportProfile(*settings, exportedAt);
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
    [strongSelf profilesChangedInApp];
    answer(brouk::ProfileManagerDelegate::ImportResult{
        created.value(), parsed.value().problems});
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
  alert.informativeText = [NSString
      stringWithFormat:@"Its sign-ins, cookies, history and settings are "
                       @"removed from this Mac. This cannot be undone.%@",
                       tabsText];
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
    std::optional<brouk::Error> error =
        strongSelf->_profiles->DeleteProfile(target);
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
    colorButton.lastItem.image = ProfileDot(std::string(colorName));
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
  const NSEventModifierFlags ctrl = NSEventModifierFlagControl;
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
            key:@","
      modifiers:cmd];
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
            key:@"t"
      modifiers:cmd];
  [self addItem:file
          title:@"New Window"
         action:@selector(newWindow:)
            key:@"n"
      modifiers:cmd];
  [self addItem:file
          title:@"Open Location…"
         action:@selector(focusLocation:)
            key:@"l"
      modifiers:cmd];
  [file addItem:NSMenuItem.separatorItem];
  [self addItem:file
          title:@"Print…"
         action:@selector(printPage:)
            key:@"p"
      modifiers:cmd];
  [file addItem:NSMenuItem.separatorItem];
  [self addItem:file
          title:@"Close Window"
         action:@selector(performClose:)
            key:@"w"
      modifiers:cmd | shift];
  [self addItem:file
          title:@"Close Tab"
         action:@selector(closeCurrentTab:)
            key:@"w"
      modifiers:cmd];

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
            key:@"f"
      modifiers:cmd];
  [self addItem:find
          title:@"Find Next"
         action:@selector(findNext:)
            key:@"g"
      modifiers:cmd];
  [self addItem:find
          title:@"Find Previous"
         action:@selector(findPrevious:)
            key:@"g"
      modifiers:cmd | shift];

  NSMenu* view = [self submenu:@"View" in:bar];
  [self addItem:view
          title:@"Stop"
         action:@selector(stopLoading:)
            key:@"."
      modifiers:cmd];
  [self addItem:view
          title:@"Reload This Page"
         action:@selector(reload:)
            key:@"r"
      modifiers:cmd];
  [self addItem:view
          title:@"Force Reload This Page"
         action:@selector(reloadIgnoringCache:)
            key:@"r"
      modifiers:cmd | shift];
  [view addItem:NSMenuItem.separatorItem];
  [self addItem:view
          title:@"Actual Size"
         action:@selector(resetZoom:)
            key:@"0"
      modifiers:cmd];
  [self addItem:view
          title:@"Zoom In"
         action:@selector(zoomIn:)
            key:@"+"
      modifiers:cmd];
  [self addItem:view
          title:@"Zoom Out"
         action:@selector(zoomOut:)
            key:@"-"
      modifiers:cmd];
  // Cmd+= zooms in too (the + key without Shift), as in Chrome.
  NSMenuItem* zoomInAlt = [self addItem:view
                                  title:@"Zoom In"
                                 action:@selector(zoomIn:)
                                    key:@"="
                              modifiers:cmd];
  zoomInAlt.hidden = YES;
  zoomInAlt.allowsKeyEquivalentWhenHidden = YES;
  [view addItem:NSMenuItem.separatorItem];
  [self addItem:view
          title:@"Enter Full Screen"
         action:@selector(toggleFullScreen:)
            key:@"f"
      modifiers:cmd | ctrl];

  // View > Developer, as in Chrome (006).
  [view addItem:NSMenuItem.separatorItem];
  NSMenu* developer = [[NSMenu alloc] initWithTitle:@"Developer"];
  [view addItemWithTitle:@"Developer" action:nil keyEquivalent:@""].submenu =
      developer;
  [self addItem:developer
          title:@"Developer Tools"
         action:@selector(toggleDevTools:)
            key:@"i"
      modifiers:cmd | opt];
  [self addItem:developer
          title:@"JavaScript Console"
         action:@selector(showDevToolsConsole:)
            key:@"j"
      modifiers:cmd | opt];
  [self addItem:developer
          title:@"Inspect Elements"
         action:@selector(pickElementToInspect:)
            key:@"c"
      modifiers:cmd | opt];

  NSMenu* history = [self submenu:@"History" in:bar];
  [self addItem:history
          title:@"Back"
         action:@selector(goBack:)
            key:@"["
      modifiers:cmd];
  [self addItem:history
          title:@"Forward"
         action:@selector(goForward:)
            key:@"]"
      modifiers:cmd];

  // Profiles (005), like Chrome's Profiles menu; filled when opened.
  NSMenu* profiles = [self submenu:@"Profiles" in:bar];
  profiles.delegate = self;

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
          title:@"Select Next Tab"
         action:@selector(selectNextTab:)
            key:[NSString
                    stringWithFormat:@"%C", (unichar)NSRightArrowFunctionKey]
      modifiers:cmd | opt];
  [self addItem:window
          title:@"Select Previous Tab"
         action:@selector(selectPreviousTab:)
            key:[NSString
                    stringWithFormat:@"%C", (unichar)NSLeftArrowFunctionKey]
      modifiers:cmd | opt];
  // Chrome's alternative tab shortcuts, not shown in the menu.
  NSMenuItem* next = [self addItem:window
                             title:@"Select Next Tab"
                            action:@selector(selectNextTab:)
                               key:@"}"
                         modifiers:cmd];
  NSMenuItem* previous = [self addItem:window
                                 title:@"Select Previous Tab"
                                action:@selector(selectPreviousTab:)
                                   key:@"{"
                             modifiers:cmd];
  for (NSMenuItem* item in @[ next, previous ]) {
    item.hidden = YES;
    item.allowsKeyEquivalentWhenHidden = YES;
  }
  for (NSInteger number = 1; number <= 9; ++number) {
    NSMenuItem* item = [self
          addItem:window
            title:[NSString stringWithFormat:@"Select Tab %ld", (long)number]
           action:@selector(selectTabByNumber:)
              key:[NSString stringWithFormat:@"%ld", (long)number]
        modifiers:cmd];
    item.tag = number;
    item.hidden = YES;
    item.allowsKeyEquivalentWhenHidden = YES;
  }
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
