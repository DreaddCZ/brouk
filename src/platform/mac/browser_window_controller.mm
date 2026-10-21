#import "platform/mac/browser_window_controller.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#import "platform/mac/address_field.h"
#import "platform/mac/browser_tab.h"
#import "platform/mac/browser_window.h"
#import "platform/mac/tab_strip_view.h"

#include "core/address_input.h"
#include "core/tab_strip_model.h"
#include "core/url_display.h"
#include "include/wrapper/cef_helpers.h"

namespace {

constexpr CGFloat kTabStripHeight = 38;
constexpr CGFloat kToolbarHeight = 40;
// Room for the close/minimize/zoom buttons at the left of the tab strip.
constexpr CGFloat kTrafficLightsInset = 78;
constexpr CGFloat kFullScreenInset = 8;

// Tab ids are unique across all windows, so a tab could later move between
// windows without clashing ids.
brouk::TabId NextTabId() {
  static brouk::TabId next_id = 1;
  return next_id++;
}

NSButton* ToolbarButton(NSString* symbol,
                        NSString* label,
                        NSString* tooltip,
                        id target,
                        SEL action) {
  NSButton* button =
      [NSButton buttonWithImage:[NSImage imageWithSystemSymbolName:symbol
                                          accessibilityDescription:label]
                         target:target
                         action:action];
  button.bezelStyle = NSBezelStyleToolbar;
  button.bordered = NO;
  button.toolTip = tooltip;
  button.symbolConfiguration = [NSImageSymbolConfiguration
      configurationWithPointSize:14
                          weight:NSFontWeightMedium];
  return button;
}

}  // namespace

// Toolbar background with a hairline separating it from the page.
@interface BRToolbarView : NSView
@end

@implementation BRToolbarView
// Fill only our bounds: since macOS 14 views do not clip to their bounds and
// dirtyRect may extend beyond them.
- (void)drawRect:(NSRect)dirtyRect {
  [NSColor.windowBackgroundColor setFill];
  NSRectFill(self.bounds);
  [NSColor.separatorColor setFill];
  NSRectFill(NSMakeRect(0, 0, self.bounds.size.width, 1));
}
@end

@interface BRBrowserWindowController () <BRTabDelegate,
                                         BRTabStripDelegate,
                                         BRAddressFieldDelegate>
@end

@implementation BRBrowserWindowController {
  __weak id<BRBrowserWindowOwner> _owner;
  std::unique_ptr<brouk::TabStripModel> _model;
  // Tabs in the strip, by id.
  NSMutableDictionary<NSNumber*, BRTab*>* _tabs;
  // Tabs removed from the strip whose browsers have not finished closing.
  NSMutableSet<BRTab*>* _closingTabs;
  // Popups being created, keyed "openerId:popupId", until CEF confirms them.
  NSMutableDictionary<NSString*, BRTab*>* _pendingPopups;
  BOOL _closingWindow;

  BRTabStripView* _tabStrip;
  BRToolbarView* _toolbar;
  NSButton* _backButton;
  NSButton* _forwardButton;
  NSButton* _reloadButton;
  BRAddressField* _addressField;
  NSView* _contentArea;
}

- (instancetype)initWithOwner:(id<BRBrowserWindowOwner>)owner {
  NSRect frame = NSMakeRect(0, 0, 1200, 800);
  NSScreen* screen = NSScreen.mainScreen;
  if (screen) {
    NSRect visible = screen.visibleFrame;
    frame.size.width = std::min<CGFloat>(1280, visible.size.width - 80);
    frame.size.height = std::min<CGFloat>(860, visible.size.height - 60);
  }
  BRBrowserWindow* window = [[BRBrowserWindow alloc]
      initWithContentRect:frame
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                          NSWindowStyleMaskMiniaturizable |
                          NSWindowStyleMaskResizable |
                          NSWindowStyleMaskFullSizeContentView
                  backing:NSBackingStoreBuffered
                    defer:NO];
  window.titlebarAppearsTransparent = YES;
  window.titleVisibility = NSWindowTitleHidden;
  window.minSize = NSMakeSize(480, 300);
  window.releasedWhenClosed = NO;
  window.tabbingMode = NSWindowTabbingModeDisallowed;
  window.collectionBehavior |= NSWindowCollectionBehaviorFullScreenPrimary;
  [window center];

  if ((self = [super initWithWindow:window])) {
    _owner = owner;
    _model = std::make_unique<brouk::TabStripModel>();
    _tabs = [NSMutableDictionary dictionary];
    _closingTabs = [NSMutableSet set];
    _pendingPopups = [NSMutableDictionary dictionary];
    window.delegate = self;
    [self buildViews];
  }
  return self;
}

- (void)buildViews {
  NSView* root = self.window.contentView;
  // CEF's browser views are layer-backed; ours must be too, or the browser
  // layers draw over the toolbar.
  root.wantsLayer = YES;
  NSRect b = root.bounds;

  _contentArea =
      [[NSView alloc] initWithFrame:NSMakeRect(0, 0, b.size.width,
                                               b.size.height - kTabStripHeight -
                                                   kToolbarHeight)];
  _contentArea.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  [root addSubview:_contentArea];

  _toolbar = [[BRToolbarView alloc]
      initWithFrame:NSMakeRect(0,
                               b.size.height - kTabStripHeight - kToolbarHeight,
                               b.size.width, kToolbarHeight)];
  _toolbar.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
  [root addSubview:_toolbar];

  _tabStrip = [[BRTabStripView alloc]
      initWithFrame:NSMakeRect(0, b.size.height - kTabStripHeight, b.size.width,
                               kTabStripHeight)];
  _tabStrip.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
  _tabStrip.leadingInset = kTrafficLightsInset;
  _tabStrip.delegate = self;
  [root addSubview:_tabStrip];

  _backButton = ToolbarButton(@"chevron.left", @"Back", @"Back (⌘[)", self,
                              @selector(goBack:));
  _forwardButton = ToolbarButton(@"chevron.right", @"Forward", @"Forward (⌘])",
                                 self, @selector(goForward:));
  _reloadButton = ToolbarButton(@"arrow.clockwise", @"Reload", @"Reload (⌘R)",
                                self, @selector(reloadOrStop:));
  CGFloat x = 8;
  for (NSButton* button in @[ _backButton, _forwardButton, _reloadButton ]) {
    button.frame = NSMakeRect(x, 6, 30, 28);
    button.autoresizingMask = NSViewMaxXMargin;
    [_toolbar addSubview:button];
    x += 32;
  }

  _addressField = [[BRAddressField alloc]
      initWithFrame:NSMakeRect(x + 6, 8, b.size.width - x - 18, 24)];
  _addressField.autoresizingMask = NSViewWidthSizable;
  _addressField.addressDelegate = self;
  [_toolbar addSubview:_addressField];
}

#pragma mark - Tabs

- (NSUInteger)liveTabCount {
  return _tabs.count + _closingTabs.count;
}

- (nullable BRTab*)activeTab {
  std::optional<brouk::TabId> active = _model->active_id();
  return active ? _tabs[@(*active)] : nil;
}

- (BRTab*)makeTab {
  BRTab* tab = [[BRTab alloc] initWithId:NextTabId() delegate:self];
  _tabs[@(tab.tabId)] = tab;
  tab.containerView.frame = _contentArea.bounds;
  tab.containerView.hidden = YES;
  [_contentArea addSubview:tab.containerView];
  return tab;
}

- (void)openTabWithURL:(nullable NSString*)url activate:(BOOL)activate {
  BRTab* tab = [self makeTab];
  _model->AppendTab(tab.tabId, activate);
  [tab createBrowserWithURL:url ?: @"about:blank"
             requestContext:[_owner requestContext]];
  [self modelDidChange];
  if (activate && !url) {
    [_addressField focusAndSelectAll];
  }
}

- (void)closeTab:(BRTab*)tab {
  if (tab && !tab.isClosing) {
    [tab requestClose];
  }
}

- (void)closeAllTabs {
  _closingWindow = YES;
  if (self.liveTabCount == 0) {
    [self.window close];
    return;
  }
  for (BRTab* tab in _tabs.allValues) {
    [self closeTab:tab];
  }
}

// Shows the active tab, hides the others, refreshes strip and toolbar.
- (void)modelDidChange {
  NSMutableArray<BRTab*>* ordered = [NSMutableArray array];
  for (std::size_t i = 0; i < _model->count(); ++i) {
    [ordered addObject:_tabs[@(_model->id_at(i))]];
  }
  BRTab* active = [self activeTab];
  for (BRTab* tab in ordered) {
    BOOL hidden = tab != active;
    if (tab.containerView.hidden != hidden) {
      tab.containerView.hidden = hidden;
    }
  }
  [_tabStrip setTabs:ordered activeTabId:active ? active.tabId : 0];
  [self updateToolbar];
}

- (void)updateToolbar {
  BRTab* tab = [self activeTab];
  _backButton.enabled = tab.canGoBack;
  _forwardButton.enabled = tab.canGoForward;
  BOOL loading = tab.isLoading;
  _reloadButton.image =
      [NSImage imageWithSystemSymbolName:loading ? @"xmark" : @"arrow.clockwise"
                accessibilityDescription:loading ? @"Stop" : @"Reload"];
  _reloadButton.toolTip = loading ? @"Stop loading (⌘.)" : @"Reload (⌘R)";
  _addressField.pageURL = tab.url ?: @"";
  [_addressField showPendingText:tab.pendingAddressText];
  NSString* title = tab.title.length ? tab.title : @"New Tab";
  self.window.title = title;
}

- (void)activateTab:(BRTab*)tab {
  if (!tab) {
    return;
  }
  BRTab* previous = [self activeTab];
  _model->ActivateTab(tab.tabId);
  if (previous != tab) {
    [self modelDidChange];
    [self focusActiveTab];
  }
}

// New and blank tabs focus the address bar, others the page (as Chrome).
- (void)focusActiveTab {
  BRTab* tab = [self activeTab];
  if (!tab) {
    return;
  }
  std::string display = brouk::FormatUrlForDisplay(tab.url.UTF8String);
  if (display.empty() || tab.pendingAddressText) {
    [_addressField focusAndSelectAll];
  } else {
    [tab focusContent];
  }
}

#pragma mark - BRTabDelegate

- (void)tabDidChangeState:(BRTab*)tab {
  [_tabStrip updateTab:tab];
  if (tab == [self activeTab]) {
    [self updateToolbar];
  }
}

- (std::optional<brouk::PopupHost>)tab:(BRTab*)opener
                            wantsPopup:(const brouk::PopupRequest&)request {
  // Chrome's popup blocker: only popups the user asked for (click, key press)
  // may open. TODO(012): address bar indicator to allow blocked popups.
  if (!request.user_gesture) {
    return std::nullopt;
  }
  BOOL foreground = request.disposition != CEF_WOD_NEW_BACKGROUND_TAB;
  BRTab* tab = [self makeTab];
  _model->InsertFromOpener(tab.tabId, opener.tabId, foreground);
  NSString* key = [NSString
      stringWithFormat:@"%lld:%d", static_cast<long long>(opener.tabId),
                       request.popup_id];
  _pendingPopups[key] = tab;
  [self modelDidChange];

  brouk::PopupHost host;
  host.parent_view = (__bridge void*)tab.containerView;
  NSRect bounds = tab.containerView.bounds;
  host.bounds = CefRect(0, 0, static_cast<int>(bounds.size.width),
                        static_cast<int>(bounds.size.height));
  host.client = [tab client];
  return host;
}

- (void)tab:(BRTab*)opener popupAborted:(int)popupId {
  NSString* key =
      [NSString stringWithFormat:@"%lld:%d",
                                 static_cast<long long>(opener.tabId), popupId];
  BRTab* tab = _pendingPopups[key];
  if (!tab) {
    return;
  }
  [_pendingPopups removeObjectForKey:key];
  [tab.containerView removeFromSuperview];
  _model->RemoveTab(tab.tabId);
  [_tabs removeObjectForKey:@(tab.tabId)];
  [self modelDidChange];
}

- (void)tabCloseReady:(BRTab*)tab {
  if (!_tabs[@(tab.tabId)]) {
    return;
  }
  BOOL wasActive = tab == [self activeTab];
  _model->RemoveTab(tab.tabId);
  [_tabs removeObjectForKey:@(tab.tabId)];
  [_closingTabs addObject:tab];
  [self modelDidChange];
  if (wasActive) {
    [self focusActiveTab];
  }
}

- (void)tabDidClose:(BRTab*)tab {
  // Also reached for a tab whose close we never saw (e.g. renderer gone).
  if (_tabs[@(tab.tabId)]) {
    [self tabCloseReady:tab];
  }
  [_closingTabs removeObject:tab];
  for (NSString* key in [_pendingPopups allKeysForObject:tab]) {
    [_pendingPopups removeObjectForKey:key];
  }
  [_owner browserWindowDidCloseBrowser];
  // Closing the last tab closes the window, as in Chrome.
  if (self.liveTabCount == 0) {
    [self.window close];
  }
}

- (BOOL)tab:(BRTab*)tab handleUnhandledKeyEvent:(NSEvent*)event {
  // Keys the page did not use go to the menu bar, as in any Mac app; this is
  // how Cmd+C, Cmd+L and other menu shortcuts work while a page has focus.
  return [NSApp.mainMenu performKeyEquivalent:event];
}

- (void)tabDidCreateBrowser:(BRTab*)tab {
  [_owner browserWindowDidCreateBrowser];
  for (NSString* key in [_pendingPopups allKeysForObject:tab]) {
    [_pendingPopups removeObjectForKey:key];
  }
}

#pragma mark - BRTabStripDelegate

- (void)tabStripDidSelectTab:(brouk::TabId)tabId {
  [self activateTab:_tabs[@(tabId)]];
}

- (void)tabStripDidCloseTab:(brouk::TabId)tabId {
  [self closeTab:_tabs[@(tabId)]];
}

- (void)tabStripDidMoveTab:(brouk::TabId)tabId toIndex:(NSUInteger)index {
  _model->MoveTab(tabId, index);
  [self modelDidChange];
}

- (void)tabStripDidRequestNewTab {
  [self newTab:nil];
}

#pragma mark - BRAddressFieldDelegate

- (void)addressField:(BRAddressField*)field didSubmit:(NSString*)text {
  BRTab* tab = [self activeTab];
  if (!tab) {
    return;
  }
  brouk::AddressInputResult result =
      brouk::ClassifyAddressInput(text.UTF8String);
  tab.pendingAddressText = nil;
  if (result.kind == brouk::AddressInputResult::Kind::kEmpty) {
    [self updateToolbar];
    return;
  }
  NSString* url = [NSString stringWithUTF8String:result.url.c_str()];
  [tab loadURL:url];
  [self updateToolbar];
  [tab focusContent];
}

- (void)addressFieldDidCancel:(BRAddressField*)field {
  [self activeTab].pendingAddressText = nil;
}

- (void)addressFieldDidEdit:(BRAddressField*)field {
  [self activeTab].pendingAddressText = field.stringValue;
}

#pragma mark - Menu and toolbar actions

- (IBAction)newTab:(id)sender {
  [self openTabWithURL:nil activate:YES];
}

- (IBAction)closeCurrentTab:(id)sender {
  [self closeTab:[self activeTab]];
}

- (IBAction)performClose:(id)sender {
  [self.window performClose:sender];
}

- (IBAction)goBack:(id)sender {
  [[self activeTab] goBack];
}

- (IBAction)goForward:(id)sender {
  [[self activeTab] goForward];
}

- (IBAction)reloadOrStop:(id)sender {
  BRTab* tab = [self activeTab];
  if (tab.isLoading) {
    [tab stopLoading];
  } else {
    [tab reload];
  }
}

- (IBAction)reload:(id)sender {
  [[self activeTab] reload];
}

- (IBAction)reloadIgnoringCache:(id)sender {
  [[self activeTab] reloadIgnoringCache];
}

- (IBAction)stopLoading:(id)sender {
  [[self activeTab] stopLoading];
}

- (IBAction)focusLocation:(id)sender {
  [_addressField focusAndSelectAll];
}

- (IBAction)selectNextTab:(id)sender {
  _model->ActivateNext();
  [self modelDidChange];
  [self focusActiveTab];
}

- (IBAction)selectPreviousTab:(id)sender {
  _model->ActivatePrevious();
  [self modelDidChange];
  [self focusActiveTab];
}

// Cmd+1 ... Cmd+9; the menu item's tag is the number.
- (IBAction)selectTabByNumber:(id)sender {
  _model->ActivateByShortcutNumber(static_cast<int>([sender tag]));
  [self modelDidChange];
  [self focusActiveTab];
}

- (BOOL)validateMenuItem:(NSMenuItem*)item {
  BRTab* tab = [self activeTab];
  SEL action = item.action;
  if (action == @selector(goBack:)) {
    return tab.canGoBack;
  }
  if (action == @selector(goForward:)) {
    return tab.canGoForward;
  }
  if (action == @selector(stopLoading:)) {
    return tab.isLoading;
  }
  if (action == @selector(selectNextTab:) ||
      action == @selector(selectPreviousTab:)) {
    return _model->count() > 1;
  }
  return YES;
}

#pragma mark - NSWindowDelegate

- (BOOL)windowShouldClose:(NSWindow*)sender {
  if (self.liveTabCount == 0) {
    return YES;
  }
  // Each page gets to run its beforeunload handler; the window closes once
  // the last tab is gone (see tabDidClose:).
  [self closeAllTabs];
  return NO;
}

- (void)windowWillClose:(NSNotification*)notification {
  [_owner browserWindowWillClose:self];
}

- (void)windowWillEnterFullScreen:(NSNotification*)notification {
  _tabStrip.leadingInset = kFullScreenInset;
  _tabStrip.needsLayout = YES;
}

- (void)windowWillExitFullScreen:(NSNotification*)notification {
  _tabStrip.leadingInset = kTrafficLightsInset;
  _tabStrip.needsLayout = YES;
}

@end
