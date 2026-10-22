#import "platform/mac/browser_window_controller.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#import "platform/mac/address_field.h"
#import "platform/mac/browser_tab.h"
#import "platform/mac/browser_window.h"
#import "platform/mac/download_notice.h"
#import "platform/mac/find_bar.h"
#import "platform/mac/profile_colors.h"
#import "platform/mac/site_info_popover.h"

#include "cef/site_info.h"
#import "platform/mac/tab_strip_view.h"

#include "core/address_input.h"
#include "core/tab_strip_model.h"
#include "core/url_display.h"
#include "core/zoom.h"
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

// Toolbar background with a line in the active tab's profile colour
// separating it from the page (005: the toolbar shows the profile).
@interface BRToolbarView : NSView
@property(nonatomic, copy) NSString* profileColorName;
@end

@implementation BRToolbarView
// Empty toolbar space moves the window, as in Chrome (the window is not
// movable by itself, see the window setup).
- (void)mouseDown:(NSEvent*)event {
  if (event.clickCount == 2) {
    [self.window performZoom:nil];
  } else {
    [self.window performWindowDragWithEvent:event];
  }
}

- (void)setProfileColorName:(NSString*)name {
  _profileColorName = [name copy];
  self.needsDisplay = YES;
}

// Fill only our bounds: since macOS 14 views do not clip to their bounds and
// dirtyRect may extend beyond them.
- (void)drawRect:(NSRect)dirtyRect {
  [NSColor.windowBackgroundColor setFill];
  NSRectFill(self.bounds);
  [brouk::mac::ProfileColor(_profileColorName.UTF8String ?: "") setFill];
  NSRectFill(NSMakeRect(0, 0, self.bounds.size.width, 2));
}
@end

@interface BRBrowserWindowController () <BRTabDelegate,
                                         BRTabStripDelegate,
                                         BRAddressFieldDelegate,
                                         BRFindBarDelegate>
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
  NSButton* _siteInfoButton;
  NSButton* _zoomButton;
  NSView* _contentArea;
  BRFindBar* _findBar;
  BRDownloadNotice* _downloadNotice;
  // The tab whose page is in HTML full screen, if any.
  __weak BRTab* _fullscreenTab;
  // We put the window into macOS full screen for that page (and leave it
  // again afterwards).
  BOOL _windowFullscreenForPage;
  NSTextField* _fullscreenHint;
  // Height of the tab strip; 0 in popup windows (window.open with features:
  // one tab, no tab strip, read-only address, as in Chrome).
  CGFloat _stripHeight;
  NSButton* _popupBlockedButton;
  NSButton* _settingsProblemButton;
  // Developer tools (006): the per-tab toggle, and the remote debugging
  // indicator.
  NSButton* _devToolsToggle;
  NSButton* _remoteDebuggingButton;
  BOOL _toggleOnLeft;
  CGFloat _addressLeft;  // Where the address area starts (after site info).
}

- (instancetype)initWithOwner:(id<BRBrowserWindowOwner>)owner {
  return [self initWithOwner:owner popupContentSize:NSZeroSize];
}

- (instancetype)initWithOwner:(id<BRBrowserWindowOwner>)owner
             popupContentSize:(NSSize)popupSize {
  BOOL isPopup = popupSize.width > 0 && popupSize.height > 0;
  NSRect frame = NSMakeRect(0, 0, 1200, 800);
  NSScreen* screen = NSScreen.mainScreen;
  NSRect visible = screen ? screen.visibleFrame : NSMakeRect(0, 0, 1280, 800);
  if (isPopup) {
    // The page asked for its content size; add our toolbar, stay on screen.
    frame.size.width =
        std::clamp<CGFloat>(popupSize.width, 260, visible.size.width);
    frame.size.height = std::clamp<CGFloat>(popupSize.height + kToolbarHeight,
                                            200, visible.size.height);
  } else {
    frame.size.width = std::min<CGFloat>(1280, visible.size.width - 80);
    frame.size.height = std::min<CGFloat>(860, visible.size.height - 60);
  }
  NSWindowStyleMask style =
      NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
      NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
  if (!isPopup) {
    style |= NSWindowStyleMaskFullSizeContentView;
  }
  BRBrowserWindow* window =
      [[BRBrowserWindow alloc] initWithContentRect:frame
                                         styleMask:style
                                           backing:NSBackingStoreBuffered
                                             defer:NO];
  if (!isPopup) {
    window.titlebarAppearsTransparent = YES;
    window.titleVisibility = NSWindowTitleHidden;
    // The tab strip fills the title bar. macOS would move the window when a
    // drag starts in the title bar's height, even on a tab; instead the
    // strip and toolbar move the window from their empty areas themselves.
    window.movable = NO;
  }
  window.minSize = isPopup ? NSMakeSize(260, 200) : NSMakeSize(480, 300);
  window.releasedWhenClosed = NO;
  window.tabbingMode = NSWindowTabbingModeDisallowed;
  // Session restore will be our own (003); macOS window restoration would
  // reopen empty windows after a crash.
  window.restorable = NO;
  window.collectionBehavior |= NSWindowCollectionBehaviorFullScreenPrimary;
  if (isPopup && NSApp.keyWindow) {
    [window cascadeTopLeftFromPoint:NSMakePoint(
                                        NSMinX(NSApp.keyWindow.frame) + 40,
                                        NSMaxY(NSApp.keyWindow.frame) - 40)];
  } else {
    [window center];
  }

  if ((self = [super initWithWindow:window])) {
    _owner = owner;
    _isPopup = isPopup;
    _stripHeight = isPopup ? 0 : kTabStripHeight;
    _model = std::make_unique<brouk::TabStripModel>([owner defaultProfileId]);
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

  _contentArea = [[NSView alloc]
      initWithFrame:NSMakeRect(0, 0, b.size.width,
                               b.size.height - _stripHeight - kToolbarHeight)];
  _contentArea.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  [root addSubview:_contentArea];

  _toolbar = [[BRToolbarView alloc]
      initWithFrame:NSMakeRect(0, b.size.height - _stripHeight - kToolbarHeight,
                               b.size.width, kToolbarHeight)];
  _toolbar.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
  [root addSubview:_toolbar];

  _tabStrip = [[BRTabStripView alloc]
      initWithFrame:NSMakeRect(0, b.size.height - kTabStripHeight, b.size.width,
                               kTabStripHeight)];
  _tabStrip.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
  _tabStrip.leadingInset = kTrafficLightsInset;
  _tabStrip.delegate = self;
  _tabStrip.hidden = _isPopup;
  [root addSubview:_tabStrip];

  _backButton = ToolbarButton(@"chevron.left", @"Back", @"Back (⌘[)", self,
                              @selector(goBack:));
  _forwardButton = ToolbarButton(@"chevron.right", @"Forward", @"Forward (⌘])",
                                 self, @selector(goForward:));
  _reloadButton = ToolbarButton(@"arrow.clockwise", @"Reload", @"Reload (⌘R)",
                                self, @selector(reloadOrStop:));
  CGFloat x = 8;
  // Popup windows show only the (read-only) address, as in Chrome.
  if (!_isPopup) {
    for (NSButton* button in @[ _backButton, _forwardButton, _reloadButton ]) {
      button.frame = NSMakeRect(x, 6, 30, 28);
      button.autoresizingMask = NSViewMaxXMargin;
      [_toolbar addSubview:button];
      x += 32;
    }
  }

  // Site info (lock) at the left of the address bar, as in Chrome.
  _siteInfoButton =
      ToolbarButton(@"info.circle", @"Site information",
                    @"View site information", self, @selector(showSiteInfo:));
  _siteInfoButton.frame = NSMakeRect(x + 4, 8, 26, 24);
  _siteInfoButton.autoresizingMask = NSViewMaxXMargin;
  _siteInfoButton.symbolConfiguration = [NSImageSymbolConfiguration
      configurationWithPointSize:12
                          weight:NSFontWeightMedium];
  [_toolbar addSubview:_siteInfoButton];
  x += 30;
  _addressLeft = x;

  _addressField = [[BRAddressField alloc]
      initWithFrame:NSMakeRect(x + 6, 8, b.size.width - x - 18, 24)];
  _addressField.autoresizingMask = NSViewWidthSizable;
  _addressField.addressDelegate = self;
  _addressField.editable = !_isPopup;
  [_toolbar addSubview:_addressField];

  // Zoom indicator at the right end of the address bar; click resets to
  // 100 % (Chrome shows a popup with the same reset button).
  _zoomButton = [NSButton buttonWithTitle:@""
                                   target:self
                                   action:@selector(resetZoom:)];
  _zoomButton.bezelStyle = NSBezelStyleInline;
  _zoomButton.font = [NSFont systemFontOfSize:11];
  _zoomButton.hidden = YES;
  _zoomButton.autoresizingMask = NSViewMinXMargin;
  _zoomButton.frame = NSMakeRect(NSMaxX(_addressField.frame) - 58, 10, 54, 20);
  [_toolbar addSubview:_zoomButton];

  // "Pop-up blocked" indicator, left of the zoom indicator (Chrome).
  _popupBlockedButton =
      ToolbarButton(@"macwindow.badge.plus", @"Pop-ups blocked",
                    @"Pop-ups were blocked on this page", self,
                    @selector(showBlockedPopups:));
  _popupBlockedButton.contentTintColor = NSColor.systemOrangeColor;
  _popupBlockedButton.hidden = YES;
  _popupBlockedButton.autoresizingMask = NSViewMinXMargin;
  _popupBlockedButton.frame =
      NSMakeRect(NSMinX(_zoomButton.frame) - 30, 8, 26, 24);
  [_toolbar addSubview:_popupBlockedButton];

  // Problems in the settings files (004: reported, not ignored); opens
  // Settings, which lists them.
  _settingsProblemButton = ToolbarButton(
      @"exclamationmark.triangle.fill", @"Problems in settings",
      @"Some settings in the settings files are invalid. Click to see them.",
      nil, @selector(openSettings:));
  _settingsProblemButton.contentTintColor = NSColor.systemYellowColor;
  _settingsProblemButton.hidden = YES;
  _settingsProblemButton.autoresizingMask = NSViewMinXMargin;
  _settingsProblemButton.frame =
      NSMakeRect(NSMinX(_popupBlockedButton.frame) - 30, 8, 26, 24);
  [_toolbar addSubview:_settingsProblemButton];

  // Dev tools toggle (006): always shown, next to the address bar.
  _devToolsToggle = ToolbarButton(@"hammer", @"Developer tools",
                                  @"Developer tools for this tab (⌥⌘I)", self,
                                  @selector(toggleDevTools:));
  [_devToolsToggle setButtonType:NSButtonTypePushOnPushOff];
  _devToolsToggle.bordered = YES;
  _devToolsToggle.bezelStyle = NSBezelStyleToolbar;
  [_toolbar addSubview:_devToolsToggle];

  // Remote debugging indicator (006): port and the all-profiles warning.
  _remoteDebuggingButton =
      [NSButton buttonWithTitle:@""
                         target:self
                         action:@selector(explainRemoteDebugging:)];
  _remoteDebuggingButton.bezelStyle = NSBezelStyleInline;
  _remoteDebuggingButton.font = [NSFont systemFontOfSize:11
                                                  weight:NSFontWeightSemibold];
  _remoteDebuggingButton.contentTintColor = NSColor.systemOrangeColor;
  _remoteDebuggingButton.hidden = YES;
  [_toolbar addSubview:_remoteDebuggingButton];
  [self layoutAddressArea];

  _findBar = [[BRFindBar alloc]
      initWithFrame:NSMakeRect(_contentArea.frame.size.width - 380,
                               NSMaxY(_contentArea.frame) - 44, 368, 36)];
  _findBar.autoresizingMask = NSViewMinXMargin | NSViewMinYMargin;
  _findBar.delegate = self;
  _findBar.hidden = YES;
  [root addSubview:_findBar];

  _downloadNotice =
      [[BRDownloadNotice alloc] initWithFrame:NSMakeRect(12, 12, 420, 40)];
  _downloadNotice.autoresizingMask = NSViewMaxXMargin | NSViewMaxYMargin;
  [root addSubview:_downloadNotice];
}

#pragma mark - Tabs

- (NSUInteger)liveTabCount {
  return _tabs.count + _closingTabs.count;
}

- (nullable BRTab*)activeTab {
  std::optional<brouk::TabId> active = _model->active_id();
  return active ? _tabs[@(*active)] : nil;
}

- (BRTab*)makeTabInProfile:(const brouk::ProfileId&)profile {
  BRTab* tab = [[BRTab alloc] initWithId:NextTabId()
                                 profile:profile
                                delegate:self];
  _tabs[@(tab.tabId)] = tab;
  tab.tabView.frame = _contentArea.bounds;
  tab.tabView.hidden = YES;
  [_contentArea addSubview:tab.tabView];
  [tab setDockPosition:[_owner dockPosition] size:[_owner dockSize]];
  return tab;
}

- (void)openTabWithURL:(nullable NSString*)url activate:(BOOL)activate {
  [self openTabWithURL:url profile:_model->default_profile() activate:activate];
}

- (void)openTabWithURL:(nullable NSString*)url
               profile:(const brouk::ProfileId&)profile
              activate:(BOOL)activate {
  BRTab* tab = [self makeTabInProfile:profile];
  _model->AppendTab(tab.tabId, profile, activate);
  [tab createBrowserWithURL:url ?: @"about:blank"
             requestContext:[_owner requestContextForProfile:profile]];
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
  BRTab* active = [self activeTab];
  NSMutableArray<BRTabGroupInfo*>* groups = [NSMutableArray array];
  for (const brouk::TabStripModel::Group& group : _model->groups()) {
    brouk::ProfileInfo info = [_owner profileInfo:group.profile];
    BRTabGroupInfo* item = [[BRTabGroupInfo alloc] init];
    item.profileId = @(group.profile.c_str());
    item.name = @(info.name.c_str());
    item.colorName = @(info.color.c_str());
    item.collapsed = group.collapsed;
    item.startIndex = group.start;
    NSMutableArray<BRTab*>* tabs = [NSMutableArray array];
    for (std::size_t i = group.start; i < group.start + group.count; ++i) {
      BRTab* tab = _tabs[@(_model->id_at(i))];
      [tabs addObject:tab];
      BOOL hidden = tab != active;
      if (tab.tabView.hidden != hidden) {
        tab.tabView.hidden = hidden;
      }
    }
    item.tabs = tabs;
    [groups addObject:item];
  }
  [_tabStrip setGroups:groups activeTabId:active ? active.tabId : 0];
  [self updateToolbar];
}

// The address field, the indicators at its right end and the dev tools
// toggle on its left or right (a setting).
- (void)layoutAddressArea {
  NSRect b = _toolbar.bounds;
  CGFloat left = _addressLeft + 6;
  CGFloat right = b.size.width - 12;
  if (_toggleOnLeft) {
    _devToolsToggle.frame = NSMakeRect(left, 7, 30, 26);
    _devToolsToggle.autoresizingMask = NSViewMaxXMargin;
    left += 34;
  } else {
    _devToolsToggle.frame = NSMakeRect(right - 30, 7, 30, 26);
    _devToolsToggle.autoresizingMask = NSViewMinXMargin;
    right -= 36;
  }
  _addressField.frame = NSMakeRect(left, 8, right - left, 24);
  CGFloat end = NSMaxX(_addressField.frame);
  _zoomButton.frame = NSMakeRect(end - 58, 10, 54, 20);
  _popupBlockedButton.frame = NSMakeRect(end - 88, 8, 26, 24);
  _settingsProblemButton.frame = NSMakeRect(end - 118, 8, 26, 24);
  [_remoteDebuggingButton sizeToFit];
  NSSize size = _remoteDebuggingButton.frame.size;
  _remoteDebuggingButton.frame =
      NSMakeRect(end - 122 - size.width, 10, size.width, 20);
  for (NSView* view in @[
         _zoomButton, _popupBlockedButton, _settingsProblemButton,
         _remoteDebuggingButton
       ]) {
    view.autoresizingMask = NSViewMinXMargin;
  }
}

- (void)setDevToolsToggleOnLeft:(BOOL)left {
  if (left != _toggleOnLeft) {
    _toggleOnLeft = left;
    [self layoutAddressArea];
  }
}

- (void)setRemoteDebuggingPort:(int)port {
  _remoteDebuggingPort = port;
  _remoteDebuggingButton.hidden = port <= 0;
  if (port > 0) {
    _remoteDebuggingButton.title =
        [NSString stringWithFormat:@"⚠ Remote debugging :%d", port];
    _remoteDebuggingButton.toolTip =
        @"Remote debugging is on: programs on this Mac can control tabs of "
        @"all profiles, including their sign-ins.";
    _remoteDebuggingButton.accessibilityLabel = _remoteDebuggingButton.toolTip;
    [self layoutAddressArea];
  }
}

- (void)explainRemoteDebugging:(id)sender {
  NSAlert* alert = [[NSAlert alloc] init];
  alert.messageText =
      [NSString stringWithFormat:@"Remote debugging is on (port %d)",
                                 _remoteDebuggingPort];
  alert.informativeText =
      @"Programs on this Mac (such as Playwright or Puppeteer) can connect to "
      @"Brouk and control tabs of all profiles, including their signed-in "
      @"sessions. Only connections from this Mac are accepted. Turn it off in "
      @"Settings > Developer tools (needs a restart).";
  [alert beginSheetModalForWindow:self.window completionHandler:nil];
}

- (void)setDockPosition:(NSString*)position size:(CGFloat)size {
  for (BRTab* tab in _tabs.allValues) {
    [tab setDockPosition:position size:size];
  }
}

- (void)setSettingsProblemCount:(NSUInteger)count {
  _settingsProblemCount = count;
  _settingsProblemButton.hidden = count == 0;
}

- (NSUInteger)tabCountInProfile:(const brouk::ProfileId&)profile {
  NSUInteger count = 0;
  for (BRTab* tab in _tabs.allValues) {
    count += tab.profileId == profile ? 1 : 0;
  }
  return count;
}

- (void)closeTabsInProfile:(const brouk::ProfileId&)profile {
  for (BRTab* tab in _tabs.allValues) {
    if (tab.profileId == profile) {
      // Without asking the page (beforeunload): the user confirmed deleting
      // the profile with its tabs.
      [tab forceClose];
    }
  }
}

- (void)profilesDidChange {
  _model->SetDefaultProfile([_owner defaultProfileId]);
  [self modelDidChange];
}

- (void)updateToolbar {
  BRTab* tab = [self activeTab];
  _devToolsToggle.state =
      tab.devToolsOn ? NSControlStateValueOn : NSControlStateValueOff;
  _devToolsToggle.accessibilityValue = tab.devToolsOn ? @"on" : @"off";
  if (tab) {
    brouk::ProfileInfo info = [_owner profileInfo:tab.profileId];
    _toolbar.profileColorName = @(info.color.c_str());
    _toolbar.accessibilityLabel =
        [NSString stringWithFormat:@"Toolbar, profile %s", info.name.c_str()];
  }
  _backButton.enabled = tab.canGoBack;
  _forwardButton.enabled = tab.canGoForward;
  BOOL loading = tab.isLoading;
  _reloadButton.image =
      [NSImage imageWithSystemSymbolName:loading ? @"xmark" : @"arrow.clockwise"
                accessibilityDescription:loading ? @"Stop" : @"Reload"];
  _reloadButton.toolTip = loading ? @"Stop loading (⌘.)" : @"Reload (⌘R)";
  _addressField.pageURL = tab.url ?: @"";
  [_addressField showPendingText:tab.pendingAddressText];
  NSString* title =
      tab.title.length && ![tab.title isEqualToString:@"about:blank"]
          ? tab.title
          : @"New Tab";
  self.window.title = title;

  CefRefPtr<CefBrowser> browser = [tab browser];
  _siteInfoButton.image = [NSImage
      imageWithSystemSymbolName:[BRSiteInfoPopover symbolForBrowser:browser]
       accessibilityDescription:[BRSiteInfoPopover labelForBrowser:browser]];
  _siteInfoButton.contentTintColor = [BRSiteInfoPopover tintForBrowser:browser];

  _popupBlockedButton.hidden = tab.blockedPopupURLs.count == 0;

  double level = [tab zoomLevel];
  _zoomButton.hidden = !brouk::IsZoomIndicatorShown(level);
  _zoomButton.title =
      [NSString stringWithFormat:@"%d %%", brouk::ZoomPercentFromLevel(level)];
  _zoomButton.toolTip = @"Zoom: click to reset to 100 % (⌘0)";
  _zoomButton.accessibilityLabel =
      [NSString stringWithFormat:@"Zoom %d percent, reset",
                                 brouk::ZoomPercentFromLevel(level)];

  _findBar.hidden = !tab.findBarVisible;
  if (tab.findBarVisible) {
    if (![_findBar.text isEqualToString:tab.findText]) {
      _findBar.text = tab.findText;
    }
    [_findBar setActiveMatch:tab.findActiveMatch ofCount:tab.findCount];
  }
}

- (void)activateTab:(BRTab*)tab {
  if (!tab) {
    return;
  }
  // Like Chrome, switching tabs ends a page's full screen.
  if (_fullscreenTab && _fullscreenTab != tab) {
    [_fullscreenTab exitFullscreen];
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
  if (tab.hasDialog) {
    [tab focusContent];  // Focuses the dialog.
  } else if (display.empty() || tab.pendingAddressText) {
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
  // window.open() with size features opens a popup window (Chrome); other
  // popups become tabs next to their opener.
  BRBrowserWindowController* target = self;
  if (request.wants_popup_window) {
    NSSize size = NSMakeSize(request.width > 0 ? request.width : 640,
                             request.height > 0 ? request.height : 480);
    target = [_owner openPopupWindowWithContentSize:size];
  }
  BOOL foreground = request.disposition != CEF_WOD_NEW_BACKGROUND_TAB;
  BRTab* tab = [target insertTabFromOpener:opener foreground:foreground];
  NSString* key = [NSString
      stringWithFormat:@"%lld:%d", static_cast<long long>(opener.tabId),
                       request.popup_id];
  _pendingPopups[key] = tab;

  brouk::PopupHost host;
  host.parent_view = (__bridge void*)tab.containerView;
  NSRect bounds = tab.containerView.bounds;
  host.bounds = CefRect(0, 0, static_cast<int>(bounds.size.width),
                        static_cast<int>(bounds.size.height));
  host.client = [tab client];
  return host;
}

// A tab for a popup or a link, placed after its opener when the opener is in
// this window. The caller creates the browser.
- (BRTab*)insertTabFromOpener:(nullable BRTab*)opener
                   foreground:(BOOL)foreground {
  // The new tab is in the opener's profile, also when the opener is in
  // another window (popup windows, 005).
  brouk::ProfileId profile =
      opener ? opener.profileId : _model->default_profile();
  BRTab* tab = [self makeTabInProfile:profile];
  if (opener && _model->IndexOf(opener.tabId)) {
    _model->InsertFromOpener(tab.tabId, opener.tabId, foreground);
  } else {
    _model->AppendTab(tab.tabId, profile, foreground);
  }
  // A tab opened from another tab inherits its dev tools state (006).
  if (opener.devToolsOn) {
    [tab setDevToolsOn:YES];
  }
  [self modelDidChange];
  return tab;
}

// Removes a tab whose browser was never created (popup creation failed).
- (void)removeUncreatedTab:(BRTab*)tab {
  [tab.tabView removeFromSuperview];
  _model->RemoveTab(tab.tabId);
  [_tabs removeObjectForKey:@(tab.tabId)];
  [self modelDidChange];
  if (self.liveTabCount == 0) {
    [self.window close];
  }
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
  // The tab may live in a popup window, owned by that window's controller.
  [static_cast<BRBrowserWindowController*>(tab.delegate)
      removeUncreatedTab:tab];
}

#pragma mark - Blocked popups

// Chrome's "Pop-ups blocked" bubble as a menu: open a blocked popup, or
// allow pop-ups for the site from now on.
- (IBAction)showBlockedPopups:(id)sender {
  BRTab* tab = [self activeTab];
  NSMenu* menu = [[NSMenu alloc] initWithTitle:@"Pop-ups blocked"];
  NSMenuItem* header = [menu addItemWithTitle:@"Pop-ups blocked on this page:"
                                       action:nil
                                keyEquivalent:@""];
  header.enabled = NO;
  for (NSString* url in tab.blockedPopupURLs) {
    NSString* title = url.length > 70 ? [[url substringToIndex:67]
                                            stringByAppendingString:@"…"]
                                      : url;
    NSMenuItem* item = [menu addItemWithTitle:title
                                       action:@selector(openBlockedPopup:)
                                keyEquivalent:@""];
    item.representedObject = url;
    item.target = self;
  }
  [menu addItem:NSMenuItem.separatorItem];
  std::string origin = brouk::GetSiteInfo([tab browser]).origin;
  if (!origin.empty()) {
    NSMenuItem* allow = [menu
        addItemWithTitle:[NSString stringWithFormat:@"Always allow pop-ups and "
                                                    @"redirects from %s",
                                                    origin.c_str()]
                  action:@selector(allowPopupsForSite:)
           keyEquivalent:@""];
    allow.target = self;
  }
  [menu
      popUpMenuPositioningItem:nil
                    atLocation:NSMakePoint(
                                   0, NSHeight(_popupBlockedButton.bounds) + 4)
                        inView:_popupBlockedButton];
}

- (void)openBlockedPopup:(NSMenuItem*)item {
  BRTab* opener = [self activeTab];
  NSString* url = item.representedObject;
  if (!opener || !url) {
    return;
  }
  BRTab* tab = [self insertTabFromOpener:opener foreground:YES];
  [tab createBrowserWithURL:url
             requestContext:[_owner requestContextForProfile:tab.profileId]];
}

- (void)allowPopupsForSite:(id)sender {
  BRTab* tab = [self activeTab];
  CefRefPtr<CefBrowser> browser = [tab browser];
  if (!browser) {
    return;
  }
  std::string origin = brouk::GetSiteInfo(browser).origin;
  brouk::SetSitePermission(browser, origin, brouk::PermissionKind::kPopups,
                           brouk::PermissionSetting::kAllow);
  [tab clearBlockedPopups];
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

- (void)tab:(BRTab*)opener
        openURL:(NSString*)url
    disposition:(brouk::OpenDisposition)disposition {
  if (disposition == brouk::OpenDisposition::kNewWindow) {
    [_owner openWindowWithURL:url profile:opener.profileId];
    return;
  }
  BOOL foreground = disposition == brouk::OpenDisposition::kNewForegroundTab;
  BRTab* tab = [self insertTabFromOpener:opener foreground:foreground];
  [tab createBrowserWithURL:url
             requestContext:[_owner requestContextForProfile:tab.profileId]];
}

#pragma mark - Developer tools (006)

- (void)tabDevToolsChanged:(BRTab*)tab {
  if (tab == [self activeTab]) {
    [self updateToolbar];
  }
}

- (void)tab:(BRTab*)tab wantsDockPosition:(NSString*)position {
  [_owner setDockPosition:position];
}

- (void)tab:(BRTab*)tab resizedDockTo:(CGFloat)size {
  [_owner setDockSize:size];
}

- (NSString*)dockPosition {
  return [_owner dockPosition];
}

- (CefRefPtr<CefRequestContext>)devToolsContext {
  return [_owner devToolsContext];
}

- (brouk::DevToolsPreferences*)devToolsPreferences {
  return [_owner devToolsPreferences];
}

- (id<BRBrowserCounter>)browserCounter {
  return _owner;
}

- (CGFloat)dockSize {
  return [_owner dockSize];
}

// The toolbar toggle and Cmd+Opt+I: dev tools on or off for this tab only.
- (IBAction)toggleDevTools:(id)sender {
  BRTab* tab = [self activeTab];
  [tab setDevToolsOn:!tab.devToolsOn];
  [self updateToolbar];
}

// Cmd+Opt+J.
- (IBAction)showDevToolsConsole:(id)sender {
  [[self activeTab] showDevToolsConsole];
  [self updateToolbar];
}

// Cmd+Opt+C.
- (IBAction)pickElementToInspect:(id)sender {
  [[self activeTab] startDevToolsElementPicker];
  [self updateToolbar];
}

- (brouk::TabClientDelegate::SearchEngine)searchEngine {
  id<BRBrowserWindowOwner> owner = _owner;
  return owner ? [owner searchEngine]
               : brouk::TabClientDelegate::SearchEngine{
                     "Google", std::string(brouk::kDefaultSearchTemplate)};
}

- (BOOL)askWhereToSaveDownloads {
  return [_owner askWhereToSaveDownloads];
}

- (std::string)tab:(BRTab*)tab
    reserveDownloadPath:(const std::string&)suggested {
  return [_owner reserveDownloadPath:suggested];
}

- (void)tab:(BRTab*)tab releaseDownloadPath:(const std::string&)path {
  [_owner releaseDownloadPath:path];
}

- (void)tab:(BRTab*)tab downloadUpdated:(const brouk::DownloadInfo&)info {
  [_downloadNotice updateWithInfo:info];
}

#pragma mark - HTML full screen

- (BOOL)isWindowFullscreen {
  return (self.window.styleMask & NSWindowStyleMaskFullScreen) != 0;
}

- (void)tab:(BRTab*)tab fullscreenChanged:(BOOL)fullscreen {
  if (fullscreen) {
    if (tab != [self activeTab]) {
      [tab exitFullscreen];
      return;
    }
    _fullscreenTab = tab;
    [self setBrowserUIHidden:YES];
    if (![self isWindowFullscreen]) {
      _windowFullscreenForPage = YES;
      [self.window toggleFullScreen:nil];
    }
    [self showFullscreenHint];
  } else if (tab == _fullscreenTab) {
    _fullscreenTab = nil;
    [self setBrowserUIHidden:NO];
    if (_windowFullscreenForPage && [self isWindowFullscreen]) {
      [self.window toggleFullScreen:nil];
    }
    _windowFullscreenForPage = NO;
  }
}

// Page full screen: the page fills the whole window.
- (void)setBrowserUIHidden:(BOOL)hidden {
  NSRect b = self.window.contentView.bounds;
  _tabStrip.hidden = hidden || _isPopup;
  _toolbar.hidden = hidden;
  _findBar.hidden = hidden || ![self activeTab].findBarVisible;
  _contentArea.frame =
      hidden ? b
             : NSMakeRect(0, 0, b.size.width,
                          b.size.height - _stripHeight - kToolbarHeight);
}

// Chrome's hint: "Press Esc to exit full screen", fading after 4 s.
- (void)showFullscreenHint {
  if (!_fullscreenHint) {
    _fullscreenHint =
        [NSTextField labelWithString:@"Press  Esc  to exit full screen"];
    _fullscreenHint.font = [NSFont systemFontOfSize:15
                                             weight:NSFontWeightMedium];
    _fullscreenHint.textColor = NSColor.whiteColor;
    _fullscreenHint.alignment = NSTextAlignmentCenter;
    _fullscreenHint.wantsLayer = YES;
    _fullscreenHint.layer.backgroundColor =
        [NSColor colorWithWhite:0.1 alpha:0.85].CGColor;
    _fullscreenHint.layer.cornerRadius = 10;
    _fullscreenHint.autoresizingMask =
        NSViewMinXMargin | NSViewMaxXMargin | NSViewMinYMargin;
  }
  NSView* root = self.window.contentView;
  NSRect b = root.bounds;
  _fullscreenHint.frame = NSMakeRect(std::floor((b.size.width - 320) / 2),
                                     b.size.height - 80, 320, 40);
  _fullscreenHint.alphaValue = 1;
  [root addSubview:_fullscreenHint];
  NSTextField* hint = _fullscreenHint;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 4 * NSEC_PER_SEC),
                 dispatch_get_main_queue(), ^{
                   [NSAnimationContext
                       runAnimationGroup:^(NSAnimationContext* context) {
                         context.duration = 0.5;
                         hint.animator.alphaValue = 0;
                       }
                       completionHandler:^{
                         if (hint.alphaValue == 0) {
                           [hint removeFromSuperview];
                         }
                       }];
                 });
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

- (void)tabStripDidToggleMute:(brouk::TabId)tabId {
  [_tabs[@(tabId)] toggleMute];
}

- (void)tabStripDidRequestNewTabInProfile:(NSString*)profileId {
  [self openTabWithURL:nil profile:profileId.UTF8String activate:YES];
}

- (void)tabStripDidToggleGroup:(NSString*)profileId {
  const brouk::ProfileId profile = profileId.UTF8String;
  BRTab* previous = [self activeTab];
  bool collapsed = false;
  for (const brouk::TabStripModel::Group& group : _model->groups()) {
    if (group.profile == profile) {
      collapsed = group.collapsed;
    }
  }
  if (!_model->SetGroupCollapsed(profile, !collapsed)) {
    return;
  }
  [self modelDidChange];
  if ([self activeTab] != previous) {
    [self focusActiveTab];
  }
}

- (void)tabStripDidDropTab:(brouk::TabId)tabId onProfile:(NSString*)profileId {
  [self moveTab:_tabs[@(tabId)] toProfile:profileId.UTF8String];
}

// Moving a tab to another profile (005): its URL reopens in that profile and
// the old tab closes; the page's session and form state do not move, so the
// user is asked first.
- (void)moveTab:(nullable BRTab*)tab
      toProfile:(const brouk::ProfileId&)profile {
  if (!tab || tab.isClosing || tab.profileId == profile) {
    [self modelDidChange];
    return;
  }
  brouk::ProfileInfo target = [_owner profileInfo:profile];
  NSAlert* alert = [[NSAlert alloc] init];
  alert.messageText = [NSString
      stringWithFormat:@"Move this tab to “%s”?", target.name.c_str()];
  alert.informativeText = [NSString
      stringWithFormat:@"The page reloads in the %s profile. You may need to "
                       @"sign in again there, and anything typed into the "
                       @"page is lost.",
                       target.name.c_str()];
  [alert addButtonWithTitle:@"Move"];
  [alert addButtonWithTitle:@"Cancel"];
  __weak BRBrowserWindowController* weakSelf = self;
  __weak BRTab* weakTab = tab;
  brouk::ProfileId targetId = profile;
  [alert beginSheetModalForWindow:self.window
                completionHandler:^(NSModalResponse response) {
                  BRBrowserWindowController* strongSelf = weakSelf;
                  BRTab* strongTab = weakTab;
                  if (!strongSelf) {
                    return;
                  }
                  if (response != NSAlertFirstButtonReturn || !strongTab ||
                      strongTab.isClosing) {
                    [strongSelf modelDidChange];  // Undo the drag.
                    return;
                  }
                  NSString* url = strongTab.url.length ? strongTab.url : nil;
                  [strongSelf openTabWithURL:url profile:targetId activate:YES];
                  [strongSelf closeTab:strongTab];
                }];
}

#pragma mark - BRAddressFieldDelegate

- (void)addressField:(BRAddressField*)field didSubmit:(NSString*)text {
  BRTab* tab = [self activeTab];
  if (!tab) {
    return;
  }
  brouk::AddressInputResult result = brouk::ClassifyAddressInput(
      text.UTF8String, [self searchEngine].url_template);
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
  if (_isPopup) {
    [_owner openNewWindow];  // Popup windows hold a single page (Chrome).
    return;
  }
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

#pragma mark - Find

- (IBAction)showFindBar:(id)sender {
  BRTab* tab = [self activeTab];
  if (!tab) {
    return;
  }
  tab.findBarVisible = YES;
  [self updateToolbar];
  [_findBar focus];
}

- (IBAction)findNext:(id)sender {
  [self findInActiveTabForward:YES];
}

- (IBAction)findPrevious:(id)sender {
  [self findInActiveTabForward:NO];
}

- (void)findInActiveTabForward:(BOOL)forward {
  BRTab* tab = [self activeTab];
  if (!tab.findBarVisible || tab.findText.length == 0) {
    [self showFindBar:nil];
    return;
  }
  [tab findText:tab.findText forward:forward findNext:YES];
}

- (void)findBar:(BRFindBar*)bar
       findText:(NSString*)text
        forward:(BOOL)forward
       findNext:(BOOL)findNext {
  [[self activeTab] findText:text forward:forward findNext:findNext];
  [self updateToolbar];
}

- (void)findBarDidClose:(BRFindBar*)bar {
  BRTab* tab = [self activeTab];
  tab.findBarVisible = NO;
  [tab stopFinding];
  [self updateToolbar];
  [tab focusContent];
}

- (IBAction)showSiteInfo:(id)sender {
  BRTab* tab = [self activeTab];
  __weak BRTab* weakTab = tab;
  [BRSiteInfoPopover showForBrowser:[tab browser]
                         relativeTo:_siteInfoButton
                             reload:^{
                               [weakTab reload];
                             }];
}

- (IBAction)printPage:(id)sender {
  [[self activeTab] print];
}

#pragma mark - Zoom

- (IBAction)zoomIn:(id)sender {
  [[self activeTab] zoom:CEF_ZOOM_COMMAND_IN];
}

- (IBAction)zoomOut:(id)sender {
  [[self activeTab] zoom:CEF_ZOOM_COMMAND_OUT];
}

- (IBAction)resetZoom:(id)sender {
  [[self activeTab] zoom:CEF_ZOOM_COMMAND_RESET];
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
  if (action == @selector(toggleDevTools:)) {
    item.state =
        tab.devToolsOn ? NSControlStateValueOn : NSControlStateValueOff;
    return tab != nil;
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
  _windowFullscreenForPage = NO;
  [_fullscreenTab exitFullscreen];
}

@end
