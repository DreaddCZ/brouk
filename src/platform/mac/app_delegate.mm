#import "platform/mac/app_delegate.h"

#include <string>
#include <vector>

#import "platform/mac/browser_window_controller.h"
#import "platform/mac/data_paths.h"

#include "brouk/app_info.h"
#include "core/address_input.h"
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

@interface BRAppDelegate () <BRBrowserWindowOwner>
@end

@implementation BRAppDelegate {
  NSMutableArray<BRBrowserWindowController*>* _windows;
  CefRefPtr<CefRequestContext> _requestContext;
  int _browserCount;
  BOOL _quitting;
}

- (instancetype)init {
  if ((self = [super init])) {
    _windows = [NSMutableArray array];
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
  // One profile ("default") until profiles (005) exist. Its data lives in its
  // own directory so later profiles are separate request contexts the same way.
  CefRequestContextSettings settings;
  CefString(&settings.cache_path) =
      brouk::mac::ProfileWebDataDirectory(@"default").UTF8String;
  _requestContext = CefRequestContext::CreateContext(settings, nullptr);

  // Like Chrome, URLs given on the command line open as tabs.
  std::vector<CefString> args;
  CefCommandLine::GetGlobalCommandLine()->GetArguments(args);
  if (args.empty()) {
    [self newWindow:nil];
    return;
  }
  BRBrowserWindowController* controller = [self openWindow];
  for (const CefString& arg : args) {
    brouk::AddressInputResult input =
        brouk::ClassifyAddressInput(arg.ToString());
    if (input.kind != brouk::AddressInputResult::Kind::kEmpty) {
      [controller openTabWithURL:@(input.url.c_str()) activate:YES];
    }
  }
  if (controller.liveTabCount == 0) {
    [controller openTabWithURL:nil activate:YES];
  }
}

- (void)relaunched {
  [NSApp activateIgnoringOtherApps:YES];
  [self newWindow:nil];
}

#pragma mark - BRBrowserWindowOwner

- (CefRefPtr<CefRequestContext>)requestContext {
  return _requestContext;
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

- (void)openWindowWithURL:(NSString*)url {
  if (_quitting || !_requestContext) {
    return;
  }
  [[self openWindow] openTabWithURL:url activate:YES];
}

- (IBAction)newWindow:(nullable id)sender {
  if (_quitting || !_requestContext) {
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
