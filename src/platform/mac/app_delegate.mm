#import "platform/mac/app_delegate.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#import "platform/mac/browser_window_controller.h"
#import "platform/mac/data_paths.h"
#import "platform/mac/profile_colors.h"
#import "platform/mac/settings_watcher.h"

#include "brouk/app_info.h"
#include "cef/browser_app.h"
#include "core/address_input.h"
#include "core/download_paths.h"
#include "core/profile_registry.h"
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

@interface BRAppDelegate () <BRBrowserWindowOwner, NSMenuDelegate>
@end

@implementation BRAppDelegate {
  NSMutableArray<BRBrowserWindowController*>* _windows;
  // Profiles (005); loaded at launch, before CEF starts.
  std::unique_ptr<brouk::ProfileRegistry> _profiles;
  // Each profile's request context (its isolated web data), created when the
  // profile is first used.
  std::map<brouk::ProfileId, CefRefPtr<CefRequestContext>> _requestContexts;
  BOOL _profilesLoaded;
  BRSettingsWatcher* _settingsWatcher;
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
  [self reportProfileProblems];
  __weak BRAppDelegate* weakSelf = self;
  _settingsWatcher = [[BRSettingsWatcher alloc]
      initWithDirectory:brouk::mac::AppDataDirectory()
                changed:^{
                  [weakSelf settingsFilesChanged];
                }];
  // Like Chrome, URLs given on the command line open as tabs.
  [self openLaunchRequest:brouk::BrowserApp::LaunchRequestFrom(
                              CefCommandLine::GetGlobalCommandLine())
             inController:nil];
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
  // Chrome's default: ~/Downloads (configurable later, see 012/004).
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
  for (BRBrowserWindowController* controller in [_windows copy]) {
    [controller closeAllTabs];
  }
  [self quitIfDone];
}

- (void)releaseBrowserObjects {
  _settingsWatcher = nil;
  [_windows removeAllObjects];
  _requestContexts.clear();
}

- (void)quitIfDone {
  if (_quitting && _browserCount <= 0 && _windows.count == 0) {
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
