#import "platform/mac/browser_window_controller.h"

#import "platform/mac/bookmark_dialogs.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>

#import "platform/mac/address_field.h"
#import "platform/mac/address_suggestions_popup.h"
#import "platform/mac/browser_tab.h"
#import "platform/mac/browser_window.h"
#import "platform/mac/download_notice.h"
#import "platform/mac/find_bar.h"
#import "platform/mac/menu_shortcuts.h"
#import "platform/mac/profile_colors.h"
#import "platform/mac/site_info_popover.h"

#include "cef/site_info.h"
#import "platform/mac/tab_strip_view.h"

#include "cef/search_suggest_request.h"
#include "core/address_input.h"
#include "core/address_suggestions.h"
#include "core/internal_pages.h"
#include "core/settings_schema.h"
#include "core/shortcuts.h"
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

// Runs a block as a button's action.
@interface BRBlockTarget : NSObject
- (instancetype)initWithBlock:(void (^)(void))block;
- (void)run:(id)sender;
@end

@implementation BRBlockTarget {
  void (^_block)(void);
}
- (instancetype)initWithBlock:(void (^)(void))block {
  if ((self = [super init])) {
    _block = [block copy];
  }
  return self;
}
- (void)run:(id)sender {
  _block();
}
@end

@interface BRBrowserWindowController () <BRTabDelegate,
                                         BRTabStripDelegate,
                                         BRAddressFieldDelegate,
                                         BRFindBarDelegate,
                                         BRSuggestionsPopupDelegate>
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
  NSButton* _starButton;  // Bookmark this tab (011).
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
  BRCommandPalette* _palette;
  // Reopen Closed Tab (013): the tabs when the whole window started closing,
  // and the profiles being deleted, whose tabs are not kept.
  std::vector<brouk::ClosedTab> _windowSnapshot;
  NSArray* _restoreBarTargets;  // Of the "Restore pages?" bar (015).
  // Tabs replaced by another tab (internal page <-> web page): closed
  // without being kept for Reopen Closed Tab.
  std::set<brouk::TabId> _replacedTabs;
  std::set<brouk::ProfileId, std::less<>> _discardedProfiles;
  // Address bar suggestions (003) for the text in `_suggestText`. Answers
  // carry `_suggestGeneration`, so late answers for older text are dropped.
  BRSuggestionsPopup* _suggestions;
  NSString* _suggestText;
  std::uint64_t _suggestGeneration;
  std::vector<brouk::HistoryMatch> _historyMatches;
  std::vector<std::string> _searchSuggestions;
  std::vector<brouk::AddressSuggestion> _suggestionRows;
  CefRefPtr<brouk::SearchSuggestRequest> _suggestRequest;
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
    static std::int64_t nextWindowId = 1;
    _windowId = nextWindowId++;
    _stripHeight = isPopup ? 0 : kTabStripHeight;
    _model = std::make_unique<brouk::TabStripModel>([owner defaultProfileId]);
    _tabs = [NSMutableDictionary dictionary];
    _closingTabs = [NSMutableSet set];
    _pendingPopups = [NSMutableDictionary dictionary];
    window.delegate = self;
    [self buildViews];
    // Tooltips name the shortcuts, which can change (003).
    [NSNotificationCenter.defaultCenter
        addObserver:self
           selector:@selector(updateToolbar)
               name:brouk::mac::kShortcutsChangedNotification
             object:nil];
    // The star shows whether the page is bookmarked (011).
    [NSNotificationCenter.defaultCenter
        addObserver:self
           selector:@selector(updateToolbar)
               name:BRBookmarksDidChangeNotification
             object:nil];
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

  // Tooltips are set in updateToolbar.
  _backButton =
      ToolbarButton(@"chevron.left", @"Back", @"", self, @selector(goBack:));
  _forwardButton = ToolbarButton(@"chevron.right", @"Forward", @"", self,
                                 @selector(goForward:));
  _reloadButton = ToolbarButton(@"arrow.clockwise", @"Reload", @"", self,
                                @selector(reloadOrStop:));
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
  _siteInfoButton = ToolbarButton(@"info.circle", @"Site information", @"",
                                  self, @selector(showSiteInfo:));
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
  _devToolsToggle = ToolbarButton(@"hammer", @"Developer tools", @"", self,
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

  // Bookmark star at the right end of the address bar, as in Chrome (011).
  _starButton = ToolbarButton(@"star", @"Bookmark this tab", @"", self,
                              @selector(bookmarkPage:));
  _starButton.symbolConfiguration = [NSImageSymbolConfiguration
      configurationWithPointSize:13
                          weight:NSFontWeightMedium];
  _starButton.hidden = YES;
  [_toolbar addSubview:_starButton];
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

- (brouk::ProfileId)activeProfileId {
  BRTab* tab = [self activeTab];
  return tab ? tab.profileId : _model->default_profile();
}

- (void)restoreClosedTab:(const brouk::ClosedTab&)closed
                activate:(BOOL)activate {
  BRTab* tab = [self makeTabInProfile:closed.profile];
  _model->InsertInGroup(tab.tabId, closed.profile, closed.position, activate);
  if (closed.pinned) {
    _model->SetPinned(tab.tabId, true);
  }
  [self startTab:tab url:@(closed.url.c_str())];
  [self modelDidChange];
  if (activate) {
    [self focusActiveTab];
  }
}

#pragma mark - Loading pages into tabs

// The internal page a URL shows, if any ("history").
- (nullable NSString*)internalPageOf:(NSString*)url {
  std::optional<std::string> page = brouk::InternalPageOf(url.UTF8String ?: "");
  return page ? @(page->c_str()) : nil;
}

// Gives a new tab its first page: an internal page in the internal context
// with its bridge, a web page in the tab's profile.
- (void)startTab:(BRTab*)tab url:(NSString*)url {
  NSString* page = [self internalPageOf:url];
  std::unique_ptr<CefMessageRouterBrowserSide::Handler> handler =
      page ? [_owner internalPageHandler:page tabId:tab.tabId controller:self]
           : nullptr;
  if (handler) {
    [tab createInternalPageWithURL:url
                             title:@"History"
                              icon:[NSImage imageWithSystemSymbolName:@"clock"
                                             accessibilityDescription:nil]
                           handler:std::move(handler)
                    requestContext:[_owner internalPagesContext]];
    return;
  }
  if (page) {
    [self openInternalPageWindow:page];
    url = @"about:blank";
  }
  [tab createBrowserWithURL:url
             requestContext:[_owner requestContextForProfile:tab.profileId]];
}

// Settings and the profile manager have their own windows.
- (void)openInternalPageWindow:(NSString*)page {
  if ([page isEqualToString:@"settings"]) {
    [_owner openSettings:nil];
  } else if ([page isEqualToString:@"profiles"]) {
    [_owner manageProfiles:nil];
  }
}

// What the address bar does with an address: web pages load in the tab;
// an internal page or a web page in an internal tab replaces the tab, so
// web content never runs in the internal context and the other way round.
- (void)navigateTab:(BRTab*)tab toURL:(NSString*)url typed:(BOOL)typed {
  NSString* page = [self internalPageOf:url];
  if (page && ![page isEqualToString:@"history"]) {
    [self openInternalPageWindow:page];
    [self updateToolbar];
    return;
  }
  if (page || tab.isInternalPage) {
    [self replaceTab:tab withURL:url typed:typed];
    return;
  }
  tab.nextVisitTyped = typed;
  [tab loadURL:url];
  [self updateToolbar];
  [tab focusContent];
}

- (void)replaceTab:(BRTab*)old withURL:(NSString*)url typed:(BOOL)typed {
  BRTab* tab = [self makeTabInProfile:old.profileId];
  _model->InsertAfter(tab.tabId, old.tabId, true);
  if (_model->IsPinned(old.tabId)) {
    _model->SetPinned(tab.tabId, true);
  }
  tab.nextVisitTyped = typed;
  [self startTab:tab url:url];
  _replacedTabs.insert(old.tabId);
  [old forceClose];
  [self modelDidChange];
  [self focusActiveTab];
}

// ⌘Y: the history page as a tab in the current tab's profile group, in
// place of an unused new tab.
- (IBAction)showHistory:(id)sender {
  [self showHistoryWithFragment:@""];
}

- (void)showHistoryWithFragment:(NSString*)fragment {
  NSString* url = fragment.length
                      ? [@"brouk://history/#" stringByAppendingString:fragment]
                      : @"brouk://history/";
  BRTab* active = [self activeTab];
  if (active.isInternalPage &&
      [[self internalPageOf:active.url] isEqualToString:@"history"]) {
    CefRefPtr<CefBrowser> browser = [active browser];
    if (fragment.length && browser) {
      // Same page: only the fragment changes (the page opens the dialog).
      browser->GetMainFrame()->LoadURL(url.UTF8String);
    }
    return;
  }
  if (active &&
      (active.url.length == 0 || [active.url isEqualToString:@"about:blank"])) {
    [self replaceTab:active withURL:url typed:NO];
    return;
  }
  brouk::ProfileId profile = [self activeProfileId];
  BRTab* tab = [self makeTabInProfile:profile];
  if (active) {
    _model->InsertAfter(tab.tabId, active.tabId, true);
  } else {
    _model->AppendTab(tab.tabId, profile, true);
  }
  [self startTab:tab url:url];
  [self modelDidChange];
}

- (CefRefPtr<CefBrowser>)anyBrowserInProfile:(const brouk::ProfileId&)profile {
  for (BRTab* tab in _tabs.allValues) {
    if (tab.profileId == profile && !tab.isInternalPage && [tab browser] &&
        !tab.isClosing) {
      return [tab browser];
    }
  }
  return nullptr;
}

- (brouk::ProfileId)profileOfTab:(brouk::TabId)tabId {
  BRTab* tab = _tabs[@(tabId)];
  return tab ? tab.profileId : _model->default_profile();
}

- (void)openPage:(NSString*)url
         profile:(const brouk::ProfileId&)profile
          newTab:(BOOL)newTab
     fromHistory:(brouk::TabId)historyTab {
  BRTab* history = _tabs[@(historyTab)];
  if (profile.empty()) {
    brouk::ProfileId own = history ? history.profileId : [self activeProfileId];
    [self openPage:url profile:own newTab:newTab fromHistory:historyTab];
    return;
  }
  if (!newTab && history && history.profileId == profile) {
    [self replaceTab:history withURL:url typed:NO];
    return;
  }
  // ⌘-click opens in the background, as Chrome does.
  BRTab* tab = [self makeTabInProfile:profile];
  if (history && history.profileId == profile) {
    _model->InsertAfter(tab.tabId, historyTab, !newTab);
  } else {
    _model->AppendTab(tab.tabId, profile, !newTab);
  }
  [self startTab:tab url:url];
  [self modelDidChange];
  if (!newTab) {
    [self focusActiveTab];
  }
}

#pragma mark - Session restore (015)

- (std::optional<brouk::SessionWindow>)sessionWindow {
  if (_isPopup || _closingWindow || _model->empty()) {
    return std::nullopt;
  }
  brouk::SessionWindow window;
  for (std::size_t i = 0; i < _model->count(); ++i) {
    BRTab* tab = _tabs[@(_model->id_at(i))];
    if (!tab) {
      continue;
    }
    if (_model->active_id() == tab.tabId) {
      window.active = window.tabs.size();
    }
    window.tabs.push_back({.url = tab.url.UTF8String ?: "",
                           .title = tab.title.UTF8String ?: "",
                           .profile = tab.profileId,
                           .pinned = _model->IsPinned(tab.tabId)});
  }
  for (const brouk::TabStripModel::Group& group : _model->groups()) {
    if (group.collapsed) {
      window.collapsed.push_back(group.profile);
    }
  }
  NSRect frame = self.window.frame;
  window.x = static_cast<int>(frame.origin.x);
  window.y = static_cast<int>(frame.origin.y);
  window.width = static_cast<int>(frame.size.width);
  window.height = static_cast<int>(frame.size.height);
  return window;
}

- (void)restoreSessionWindow:(const brouk::SessionWindow&)window {
  if (window.width > 0) {
    NSRect frame = NSMakeRect(window.x, window.y, window.width, window.height);
    // Only if it is still on a screen (displays may have changed).
    for (NSScreen* screen in NSScreen.screens) {
      if (NSIntersectsRect(screen.visibleFrame, frame)) {
        [self.window setFrame:frame display:NO];
        break;
      }
    }
  }
  std::optional<brouk::TabId> active;
  for (std::size_t i = 0; i < window.tabs.size(); ++i) {
    const brouk::SessionTab& saved = window.tabs[i];
    brouk::ProfileId profile = saved.profile;
    if (![_owner profileExists:profile]) {
      profile = [_owner defaultProfileId];
    }
    BRTab* tab = [self makeTabInProfile:profile];
    _model->AppendTab(tab.tabId, profile, false);
    if (saved.pinned) {
      _model->SetPinned(tab.tabId, true);
    }
    NSString* url = @(saved.url.c_str()) ?: @"about:blank";
    CefRefPtr<CefRequestContext> context =
        [_owner requestContextForProfile:profile];
    if (i == window.active || [self internalPageOf:url]) {
      if (i == window.active) {
        active = tab.tabId;
      }
      [self startTab:tab url:url];
    } else {
      [tab deferBrowserWithURL:url
                         title:@(saved.title.c_str()) ?: @""
                requestContext:context];
    }
  }
  if (active) {
    _model->ActivateTab(*active);
  }
  for (const brouk::ProfileId& profile : window.collapsed) {
    _model->SetGroupCollapsed(profile, true);
  }
  [self modelDidChange];
}

- (BOOL)hasOnlyNewTab {
  if (_model->count() != 1) {
    return NO;
  }
  NSString* url = [self activeTab].url;
  return url.length == 0 || [url isEqualToString:@"about:blank"];
}

- (void)showRestorePagesBar:(void (^)(void))restore {
  NSView* content = self.window.contentView;
  NSRect contentBounds = content.bounds;
  CGFloat top = NSMaxY(contentBounds) - _stripHeight - kToolbarHeight;
  NSVisualEffectView* bar = [[NSVisualEffectView alloc]
      initWithFrame:NSMakeRect(0, top - 40, contentBounds.size.width, 40)];
  bar.material = NSVisualEffectMaterialHeaderView;
  bar.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
  bar.accessibilityRole = NSAccessibilityGroupRole;
  bar.accessibilityLabel = @"Restore pages";
  NSTextField* text = [NSTextField
      labelWithString:@"Restore pages?  Brouk didn't shut down correctly."];
  text.frame = NSMakeRect(16, 11, 400, 18);
  NSButton* restoreButton = [NSButton buttonWithTitle:@"Restore"
                                               target:nil
                                               action:nil];
  restoreButton.frame = NSMakeRect(contentBounds.size.width - 150, 6, 90, 28);
  restoreButton.autoresizingMask = NSViewMinXMargin;
  restoreButton.keyEquivalent = @"";
  NSButton* close =
      [NSButton buttonWithImage:[NSImage imageWithSystemSymbolName:@"xmark"
                                          accessibilityDescription:@"Close"]
                         target:nil
                         action:nil];
  close.bordered = NO;
  close.frame = NSMakeRect(contentBounds.size.width - 48, 8, 24, 24);
  close.autoresizingMask = NSViewMinXMargin;
  for (NSView* view in @[ text, restoreButton, close ]) {
    [bar addSubview:view];
  }
  [content addSubview:bar positioned:NSWindowAbove relativeTo:nil];
  __weak NSView* weakBar = bar;
  void (^dismiss)(void) = ^{
    [weakBar removeFromSuperview];
  };
  // Buttons hold their targets weakly: the controller keeps them.
  BRBlockTarget* restoreTarget = [[BRBlockTarget alloc] initWithBlock:^{
    dismiss();
    restore();
  }];
  BRBlockTarget* closeTarget = [[BRBlockTarget alloc] initWithBlock:dismiss];
  _restoreBarTargets = @[ restoreTarget, closeTarget ];
  restoreButton.target = restoreTarget;
  restoreButton.action = @selector(run:);
  close.target = closeTarget;
  close.action = @selector(run:);
}

// History → Home: the new tab page in the current tab, as in Chrome.
- (IBAction)goHome:(id)sender {
  BRTab* tab = [self activeTab];
  if (!tab) {
    return;
  }
  [self navigateTab:tab toURL:@"about:blank" typed:NO];
  [_addressField focusAndSelectAll];
}

- (void)openTabWithURL:(nullable NSString*)url activate:(BOOL)activate {
  [self openTabWithURL:url profile:_model->default_profile() activate:activate];
}

- (void)openTabWithURL:(nullable NSString*)url
               profile:(const brouk::ProfileId&)profile
              activate:(BOOL)activate {
  BRTab* tab = [self makeTabInProfile:profile];
  _model->AppendTab(tab.tabId, profile, activate);
  [self startTab:tab url:url ?: @"about:blank"];
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

// The tab as Reopen Closed Tab needs it; `index` is its model index.
- (brouk::ClosedTab)closedTabFor:(BRTab*)tab atIndex:(std::size_t)index {
  std::size_t start = index;
  for (const brouk::TabStripModel::Group& group : _model->groups()) {
    if (group.profile == tab.profileId) {
      start = group.start;
    }
  }
  return {.url = tab.url.UTF8String ?: "",
          .title = tab.title.UTF8String ?: "",
          .profile = tab.profileId,
          .pinned = _model->IsPinned(tab.tabId),
          .window_id = _windowId,
          .position = index - start};
}

- (void)closeAllTabs {
  if (!_closingWindow) {
    _windowSnapshot.clear();
    for (std::size_t i = 0; i < _model->count(); ++i) {
      BRTab* tab = _tabs[@(_model->id_at(i))];
      if (tab && !_discardedProfiles.contains(tab.profileId)) {
        _windowSnapshot.push_back([self closedTabFor:tab atIndex:i]);
      }
    }
  }
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
  // A restored tab gets its browser when first shown (015).
  if (active.hasDeferredBrowser) {
    [active createDeferredBrowser];
  }
  [_owner browserWindowSessionChanged];
  NSMutableArray<BRTabGroupInfo*>* groups = [NSMutableArray array];
  for (const brouk::TabStripModel::Group& group : _model->groups()) {
    brouk::ProfileInfo info = [_owner profileInfo:group.profile];
    BRTabGroupInfo* item = [[BRTabGroupInfo alloc] init];
    item.profileId = @(group.profile.c_str());
    item.name = @(info.name.c_str());
    item.colorName = @(info.color.c_str());
    item.collapsed = group.collapsed;
    item.startIndex = group.start;
    item.pinnedCount = group.pinned_count;
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
  _starButton.frame = NSMakeRect(end - 28, 8, 26, 24);
  _zoomButton.frame = NSMakeRect(end - 86, 10, 54, 20);
  _popupBlockedButton.frame = NSMakeRect(end - 116, 8, 26, 24);
  _settingsProblemButton.frame = NSMakeRect(end - 146, 8, 26, 24);
  [_remoteDebuggingButton sizeToFit];
  NSSize size = _remoteDebuggingButton.frame.size;
  _remoteDebuggingButton.frame =
      NSMakeRect(end - 150 - size.width, 10, size.width, 20);
  for (NSView* view in @[
         _starButton, _zoomButton, _popupBlockedButton, _settingsProblemButton,
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
  _discardedProfiles.insert(profile);
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
  // On: filled and in the accent colour, so the state is visible at once.
  // A request rule changed this page: orange (006: never invisible).
  BOOL ruled = tab.devToolsOn && tab.rulesAffectedPage;
  _devToolsToggle.accessibilityValue =
      ruled ? @"on, request rules changed this page"
            : (tab.devToolsOn ? @"on" : @"off");
  _devToolsToggle.toolTip = brouk::mac::ToolTip(
      ruled ? @"Request rules changed this page (developer tools)"
            : @"Developer tools for this tab",
      "devtools");
  _devToolsToggle.image =
      [NSImage imageWithSystemSymbolName:ruled ? @"hammer.circle.fill"
                                         : tab.devToolsOn ? @"hammer.fill"
                                                          : @"hammer"
                accessibilityDescription:@"Developer tools"];
  _devToolsToggle.contentTintColor = ruled ? NSColor.systemOrangeColor
                                     : tab.devToolsOn
                                         ? NSColor.controlAccentColor
                                         : NSColor.secondaryLabelColor;
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
  _reloadButton.toolTip = loading ? brouk::mac::ToolTip(@"Stop loading", "stop")
                                  : brouk::mac::ToolTip(@"Reload", "reload");
  _backButton.toolTip = brouk::mac::ToolTip(@"Back", "back");
  _forwardButton.toolTip = brouk::mac::ToolTip(@"Forward", "forward");
  _siteInfoButton.toolTip =
      brouk::mac::ToolTip(@"View site information", "site_info");
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

  BOOL bookmarkable = [self canBookmarkTab:tab];
  _starButton.hidden = !bookmarkable;
  if (bookmarkable) {
    BOOL bookmarked =
        ![_owner bookmarkStore]
             .model.BookmarksWithUrl(tab.url.UTF8String, tab.profileId)
             .empty();
    _starButton.image =
        [NSImage imageWithSystemSymbolName:bookmarked ? @"star.fill" : @"star"
                  accessibilityDescription:bookmarked ? @"Edit bookmark"
                                                      : @"Bookmark this tab"];
    _starButton.contentTintColor =
        bookmarked ? NSColor.controlAccentColor : nil;
    _starButton.toolTip = brouk::mac::ToolTip(
        bookmarked ? @"Edit bookmark for this tab" : @"Bookmark this tab",
        "bookmark_page");
  }

  double level = [tab zoomLevel];
  _zoomButton.hidden = !brouk::IsZoomIndicatorShown(level);
  _zoomButton.title =
      [NSString stringWithFormat:@"%d %%", brouk::ZoomPercentFromLevel(level)];
  _zoomButton.toolTip =
      brouk::mac::ToolTip(@"Zoom: click to reset to 100 %", "zoom_reset");
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
  [_owner browserWindowSessionChanged];
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
  // A tab closed on its own can be reopened; a window closing as a whole is
  // kept as one entry (windowWillClose:).
  std::optional<std::size_t> index = _model->IndexOf(tab.tabId);
  bool replaced = _replacedTabs.erase(tab.tabId) > 0;
  if (index && !replaced && !_closingWindow &&
      !_discardedProfiles.contains(tab.profileId)) {
    [_owner browserWindow:self
                closedTab:[self closedTabFor:tab atIndex:*index]];
  }
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
  // A restored tab never shown had no browser to count.
  if (tab.hasCreatedBrowser) {
    [_owner browserWindowDidCloseBrowser];
  }
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

- (NSString*)secretHeaderNames {
  return [_owner secretHeaderNames];
}

- (brouk::ToolsHost::Rules)requestRulesForProfile:
    (const brouk::ProfileId&)profile {
  id<BRBrowserWindowOwner> owner = _owner;
  return owner ? [owner requestRulesForProfile:profile]
               : brouk::ToolsHost::Rules{};
}

- (std::optional<brouk::Error>)setRequestRules:(brouk::RequestRuleSet)rules
                                    forProfile:
                                        (const brouk::ProfileId&)profile {
  id<BRBrowserWindowOwner> owner = _owner;
  if (!owner) {
    return brouk::Error{"Brouk is closing."};
  }
  return [owner setRequestRules:std::move(rules) forProfile:profile];
}

- (void)requestRulesChanged:(const brouk::RequestRuleSet&)rules
                 forProfile:(const brouk::ProfileId&)profile {
  for (BRTab* tab in _tabs.allValues) {
    if (tab.profileId == profile) {
      [tab requestRulesChanged:rules];
    }
  }
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

- (void)sendRendererConfigWithJsonViewer:(BOOL)jsonViewer {
  for (BRTab* tab in _tabs.allValues) {
    [tab sendRendererConfigWithJsonViewer:jsonViewer];
  }
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

- (void)tab:(BRTab*)tab
    visitedURL:(const std::string&)url
         title:(const std::string&)title
    transition:(brouk::VisitTransition)transition {
  if (brouk::HistoryService* history = [_owner historyService]) {
    history->RecordVisit(tab.profileId, url, title, transition);
    [_owner historyChangedForProfile:tab.profileId];
  }
}

- (void)tab:(BRTab*)tab visitRedirectedFrom:(const std::string&)url {
  if (brouk::HistoryService* history = [_owner historyService]) {
    history->RemoveRedirectSource(tab.profileId, url);
    [_owner historyChangedForProfile:tab.profileId];
  }
}

- (void)tab:(BRTab*)tab titleChanged:(NSString*)title forURL:(NSString*)url {
  if (brouk::HistoryService* history = [_owner historyService]) {
    history->SetTitle(tab.profileId, url.UTF8String ?: "",
                      title.UTF8String ?: "");
  }
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

- (void)tabStripDidActivateGroup:(NSString*)profileId {
  std::optional<brouk::TabId> tabId =
      _model->LastActiveTabOf(profileId.UTF8String);
  if (tabId) {
    [self activateTab:_tabs[@(*tabId)]];
  }
}

- (void)tabStripDidSetPinned:(BOOL)pinned tab:(brouk::TabId)tabId {
  if (_model->SetPinned(tabId, pinned)) {
    [self modelDidChange];
  }
}

- (void)tabStripDidReloadTab:(brouk::TabId)tabId {
  [_tabs[@(tabId)] reload];
}

- (void)tabStripDidRequestNewTabAfter:(brouk::TabId)tabId {
  [self openTabAfter:_tabs[@(tabId)] withURL:nil];
}

// Like Chrome's Duplicate, but only the address: CEF cannot copy the
// back/forward history into a new browser.
- (void)tabStripDidDuplicateTab:(brouk::TabId)tabId {
  BRTab* tab = _tabs[@(tabId)];
  [self openTabAfter:tab withURL:tab.url.length ? tab.url : nil];
}

- (void)tabStripDidCloseTabs:(brouk::TabStripModel::CloseScope)scope
                     fromTab:(brouk::TabId)tabId {
  for (brouk::TabId other : _model->TabsToClose(tabId, scope)) {
    [self closeTab:_tabs[@(other)]];
  }
}

// A new active tab right after `after`, in its profile.
- (void)openTabAfter:(nullable BRTab*)after withURL:(nullable NSString*)url {
  if (!after) {
    return;
  }
  BRTab* tab = [self makeTabInProfile:after.profileId];
  _model->InsertAfter(tab.tabId, after.tabId, true);
  [self startTab:tab url:url ?: @"about:blank"];
  [self modelDidChange];
  if (!url) {
    [_addressField focusAndSelectAll];
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
  if (_suggestions.visible && _suggestions.selectedRow >= 0) {
    [self openSuggestionRow:_suggestions.selectedRow];
    return;
  }
  [self closeSuggestions];
  brouk::AddressInputResult result = brouk::ClassifyAddressInput(
      text.UTF8String, [self searchEngine].url_template);
  tab.pendingAddressText = nil;
  if (result.kind == brouk::AddressInputResult::Kind::kEmpty) {
    [self updateToolbar];
    return;
  }
  NSString* url = [NSString stringWithUTF8String:result.url.c_str()];
  // Typed addresses count for history ranking; searches do not (Chrome).
  [self navigateTab:tab
              toURL:url
              typed:result.kind == brouk::AddressInputResult::Kind::kUrl];
}

- (void)addressFieldDidCancel:(BRAddressField*)field {
  [self activeTab].pendingAddressText = nil;
  [self closeSuggestions];
}

- (void)addressFieldDidEdit:(BRAddressField*)field {
  [self activeTab].pendingAddressText = field.stringValue;
  [self updateSuggestions];
}

- (void)addressFieldDidEndEditing:(BRAddressField*)field {
  [self closeSuggestions];
}

#pragma mark - Address bar suggestions (003)

- (void)closeSuggestions {
  ++_suggestGeneration;
  if (_suggestRequest) {
    _suggestRequest->Cancel();
    _suggestRequest = nullptr;
  }
  _historyMatches.clear();
  _searchSuggestions.clear();
  _suggestionRows.clear();
  _suggestText = nil;
  [_suggestions close];
}

// Asks the sources again for the typed text: the profile's history, then
// the search engine.
- (void)updateSuggestions {
  BRTab* tab = [self activeTab];
  NSString* typed = _addressField.typedText;
  NSString* trimmed = [typed
      stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
  if (!tab || trimmed.length == 0 || _isPopup) {
    [self closeSuggestions];
    return;
  }
  std::uint64_t generation = ++_suggestGeneration;
  _suggestText = [typed copy];
  _historyMatches.clear();
  [self showSuggestions];

  __weak BRBrowserWindowController* weakSelf = self;
  brouk::HistoryService* history = [_owner historyService];
  if (history && [_owner historySuggestionsEnabled]) {
    history->Suggest(
        tab.profileId, typed.UTF8String ?: "", brouk::kMaxHistorySuggestions,
        [weakSelf, generation](std::vector<brouk::HistoryMatch> m) {
          [weakSelf historySuggestions:std::move(m) generation:generation];
        });
  }
  if (_suggestRequest) {
    _suggestRequest->Cancel();
    _suggestRequest = nullptr;
  }
  std::string suggestTemplate = [_owner searchSuggestTemplate];
  std::string text = typed.UTF8String ?: "";
  if (suggestTemplate.empty() || !brouk::ShouldFetchSearchSuggestions(text)) {
    _searchSuggestions.clear();
    return;
  }
  _suggestRequest = brouk::SearchSuggestRequest::Start(
      [_owner requestContextForProfile:tab.profileId],
      brouk::BuildSearchUrl(text, suggestTemplate),
      [weakSelf, generation](std::vector<std::string> suggestions) {
        [weakSelf searchSuggestions:std::move(suggestions)
                         generation:generation];
      });
}

- (void)historySuggestions:(std::vector<brouk::HistoryMatch>)matches
                generation:(std::uint64_t)generation {
  if (generation != _suggestGeneration) {
    return;
  }
  _historyMatches = std::move(matches);
  [self showSuggestions];
}

- (void)searchSuggestions:(std::vector<std::string>)suggestions
               generation:(std::uint64_t)generation {
  if (generation != _suggestGeneration) {
    return;
  }
  _suggestRequest = nullptr;
  _searchSuggestions = std::move(suggestions);
  [self showSuggestions];
}

- (void)showSuggestions {
  if (!_suggestText || !_addressField.isEditing) {
    return;
  }
  brouk::TabClientDelegate::SearchEngine engine = [self searchEngine];
  std::string typed = _suggestText.UTF8String ?: "";
  _suggestionRows = brouk::BuildAddressSuggestions(
      {.text = typed,
       .search_template = engine.url_template,
       .search_engine_name = engine.name,
       .history = _historyMatches,
       .search_suggestions = _searchSuggestions});
  if (!_suggestions) {
    _suggestions = [[BRSuggestionsPopup alloc] init];
    _suggestions.delegate = self;
  }
  [_suggestions showRows:_suggestionRows belowView:_addressField];
  if (!_suggestionRows.empty() &&
      !_suggestionRows.front().inline_completion.empty()) {
    [_addressField
        setInlineCompletion:@(_suggestionRows.front().inline_completion.c_str())
               forTypedText:_suggestText];
  }
}

// What the address field shows while a row is selected with Up/Down.
- (nullable NSString*)fieldTextForRow:(NSInteger)row {
  const brouk::AddressSuggestion& suggestion =
      _suggestionRows[static_cast<std::size_t>(row)];
  switch (suggestion.kind) {
    case brouk::AddressSuggestion::Kind::kHistory:
      return @(suggestion.url.c_str());
    case brouk::AddressSuggestion::Kind::kSearch:
      return @(suggestion.text.c_str());
    case brouk::AddressSuggestion::Kind::kTypedUrl:
    case brouk::AddressSuggestion::Kind::kTypedSearch:
      return nil;  // The typed text.
  }
  return nil;
}

- (BOOL)addressField:(BRAddressField*)field doCommand:(SEL)command {
  if (!_suggestions.visible || _suggestionRows.empty()) {
    return NO;
  }
  NSInteger count = static_cast<NSInteger>(_suggestionRows.size());
  if (command == @selector(moveDown:) || command == @selector(moveUp:)) {
    NSInteger row =
        _suggestions.selectedRow + (command == @selector(moveDown:) ? 1 : -1);
    row = std::clamp<NSInteger>(row, 0, count - 1);
    _suggestions.selectedRow = row;
    [_addressField showSuggestionText:[self fieldTextForRow:row]];
    return YES;
  }
  if (command == @selector(cancelOperation:)) {
    // First Escape closes the list and drops the inline completion; the
    // next one restores the page address (the field's own Escape).
    [_addressField showSuggestionText:nil];
    [self closeSuggestions];
    return YES;
  }
  // ⇧⌫ removes a history suggestion from the profile's history (013).
  NSInteger row = _suggestions.selectedRow;
  if (row >= 0 && row < count &&
      _suggestionRows[static_cast<std::size_t>(row)].kind ==
          brouk::AddressSuggestion::Kind::kHistory) {
    std::string url = _suggestionRows[static_cast<std::size_t>(row)].url;
    BRTab* tab = [self activeTab];
    if (brouk::HistoryService* history = [_owner historyService];
        history && tab) {
      history->DeletePage(tab.profileId, url);
      [_owner historyChangedForProfile:tab.profileId];
    }
    std::erase_if(_historyMatches, [&url](const brouk::HistoryMatch& match) {
      return match.url == url;
    });
    [_addressField showSuggestionText:nil];
    [self showSuggestions];
    return YES;
  }
  return NO;
}

- (void)suggestionsPopup:(BRSuggestionsPopup*)popup
            didChooseRow:(NSInteger)row {
  [self openSuggestionRow:row];
}

// Loads a row in the active tab, as Return does.
- (void)openSuggestionRow:(NSInteger)row {
  BRTab* tab = [self activeTab];
  if (!tab || row < 0 ||
      row >= static_cast<NSInteger>(_suggestionRows.size())) {
    return;
  }
  brouk::AddressSuggestion suggestion =
      _suggestionRows[static_cast<std::size_t>(row)];
  [self closeSuggestions];
  tab.pendingAddressText = nil;
  // Addresses and history pages count as typed (Chrome); searches do not.
  [self
      navigateTab:tab
            toURL:@(suggestion.url.c_str())
            typed:suggestion.kind ==
                      brouk::AddressSuggestion::Kind::kTypedUrl ||
                  suggestion.kind == brouk::AddressSuggestion::Kind::kHistory];
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

// Tab > Pin Tab / Unpin Tab.
#pragma mark - Command palette (003)

- (IBAction)showCommandPalette:(id)sender {
  if (_isPopup) {
    return;
  }
  if (!_palette) {
    _palette = [[BRCommandPalette alloc] init];
  }
  NSMutableArray<BRPaletteEntry*>* entries = [NSMutableArray array];
  for (BRBrowserWindowController* controller in
       [_owner browserWindowControllers]) {
    [entries addObjectsFromArray:[controller paletteTabEntries]];
  }
  [entries addObjectsFromArray:[self paletteActionEntries]];
  [entries addObjectsFromArray:[self paletteSettingEntries]];
  [_palette showInWindow:self.window entries:entries];
}

- (NSArray<BRPaletteEntry*>*)paletteTabEntries {
  NSMutableArray<BRPaletteEntry*>* entries = [NSMutableArray array];
  __weak BRBrowserWindowController* weakSelf = self;
  for (std::size_t i = 0; i < _model->count(); ++i) {
    BRTab* tab = _tabs[@(_model->id_at(i))];
    if (!tab || tab.isClosing) {
      continue;
    }
    brouk::ProfileInfo info = [_owner profileInfo:tab.profileId];
    BRPaletteEntry* entry = [[BRPaletteEntry alloc] init];
    entry.kind = brouk::PaletteItem::Kind::kTab;
    entry.title =
        tab.title.length ? tab.title : (tab.url.length ? tab.url : @"New Tab");
    std::string address = brouk::FormatUrlForDisplay(tab.url.UTF8String ?: "");
    entry.detail = [NSString
        stringWithFormat:@"%s · %s", info.name.c_str(), address.c_str()];
    entry.image = tab.favicon ?: brouk::mac::ProfileDot(info.color);
    brouk::TabId tabId = tab.tabId;
    entry.run = ^{
      BRBrowserWindowController* strongSelf = weakSelf;
      [strongSelf showWindow:nil];
      [strongSelf activateTab:strongSelf->_tabs[@(tabId)]];
    };
    [entries addObject:entry];
  }
  return entries;
}

// The browser actions (shortcut actions) that can run now, as their menu
// items would.
- (NSArray<BRPaletteEntry*>*)paletteActionEntries {
  NSMutableArray<BRPaletteEntry*>* entries = [NSMutableArray array];
  NSImage* icon = [NSImage imageWithSystemSymbolName:@"command"
                            accessibilityDescription:nil];
  for (const brouk::ShortcutAction& action : brouk::AllShortcutActions()) {
    std::string_view actionId = action.id;
    if (actionId.starts_with("select_") || actionId == "command_palette") {
      continue;  // Tab numbers are noise here; the palette is open.
    }
    SEL selector = nil;
    id target = nil;
    NSMenuItem* item = brouk::mac::MenuItemForAction(actionId);
    if (item) {
      target = [NSApp targetForAction:item.action to:item.target from:item];
      if (!target ||
          ([target respondsToSelector:@selector(validateMenuItem:)] &&
           ![target validateMenuItem:item])) {
        continue;
      }
      selector = item.action;
    } else if (actionId == "manage_profiles") {
      selector = @selector(manageProfiles:);  // In the Profiles menu.
    } else {
      continue;
    }
    BRPaletteEntry* entry = [[BRPaletteEntry alloc] init];
    entry.kind = brouk::PaletteItem::Kind::kAction;
    entry.title = @(std::string(action.label).c_str());
    entry.detail = @"";
    entry.image = icon;
    const std::vector<brouk::Shortcut>& shortcuts =
        brouk::mac::ShortcutsFor(actionId);
    if (!shortcuts.empty()) {
      entry.shortcut = @(brouk::DisplayShortcut(shortcuts[0]).c_str());
    }
    entry.run = ^{
      [NSApp sendAction:selector to:nil from:item];
    };
    [entries addObject:entry];
  }
  return entries;
}

// Every setting on the settings page; choosing one opens the page there.
- (NSArray<BRPaletteEntry*>*)paletteSettingEntries {
  NSMutableArray<BRPaletteEntry*>* entries = [NSMutableArray array];
  NSImage* icon = [NSImage imageWithSystemSymbolName:@"gearshape"
                            accessibilityDescription:nil];
  BRTab* active = [self activeTab];
  brouk::ProfileId profile =
      active ? active.profileId : _model->default_profile();
  NSString* profileId = @(profile.c_str());
  brouk::ProfileInfo info = [_owner profileInfo:profile];
  __weak id<BRBrowserWindowOwner> owner = _owner;
  for (const brouk::SettingDef& def : brouk::AllSettingDefs()) {
    if (def.advanced) {
      continue;
    }
    BOOL perProfile = def.scope == brouk::SettingScope::kProfile;
    BRPaletteEntry* entry = [[BRPaletteEntry alloc] init];
    entry.kind = brouk::PaletteItem::Kind::kSetting;
    entry.title = @(std::string(def.label).c_str());
    entry.detail =
        perProfile
            ? [NSString stringWithFormat:@"Settings · %s (%s)",
                                         std::string(def.section).c_str(),
                                         info.name.c_str()]
            : [NSString stringWithFormat:@"Settings · %s",
                                         std::string(def.section).c_str()];
    entry.image = icon;
    NSString* key = @(std::string(def.key).c_str());
    entry.run = ^{
      [owner openSettingsAtKey:key profile:perProfile ? profileId : nil];
    };
    [entries addObject:entry];
  }
  return entries;
}

#pragma mark - Tab actions for the active tab (Tab menu, palette)

- (IBAction)newTabToTheRight:(id)sender {
  if (BRTab* tab = [self activeTab]) {
    [self tabStripDidRequestNewTabAfter:tab.tabId];
  }
}

- (IBAction)duplicateTab:(id)sender {
  if (BRTab* tab = [self activeTab]) {
    [self tabStripDidDuplicateTab:tab.tabId];
  }
}

- (IBAction)closeOtherTabs:(id)sender {
  [self closeTabsOfActiveTab:brouk::TabStripModel::CloseScope::kOthers];
}

- (IBAction)closeTabsToTheLeft:(id)sender {
  [self closeTabsOfActiveTab:brouk::TabStripModel::CloseScope::kLeft];
}

- (IBAction)closeTabsToTheRight:(id)sender {
  [self closeTabsOfActiveTab:brouk::TabStripModel::CloseScope::kRight];
}

- (void)closeTabsOfActiveTab:(brouk::TabStripModel::CloseScope)scope {
  if (BRTab* tab = [self activeTab]) {
    [self tabStripDidCloseTabs:scope fromTab:tab.tabId];
  }
}

- (BOOL)activeTabCanClose:(brouk::TabStripModel::CloseScope)scope {
  BRTab* tab = [self activeTab];
  return tab && !_model->TabsToClose(tab.tabId, scope).empty();
}

// Tab > Mute Tab.
- (IBAction)toggleMuteTab:(id)sender {
  [[self activeTab] toggleMute];
}

- (IBAction)togglePinTab:(id)sender {
  BRTab* tab = [self activeTab];
  if (tab) {
    [self tabStripDidSetPinned:!_model->IsPinned(tab.tabId) tab:tab.tabId];
  }
}

// Cmd+1 ... Cmd+9; the menu item's tag is the number.
- (IBAction)selectTabByNumber:(id)sender {
  _model->ActivateByShortcutNumber(static_cast<int>([sender tag]));
  [self modelDidChange];
  [self focusActiveTab];
}

#pragma mark - Bookmarks (011)

// Web pages can be bookmarked; new tab pages and internal pages cannot.
- (BOOL)canBookmarkTab:(nullable BRTab*)tab {
  return tab && !_isPopup && !tab.isInternalPage && tab.url.length > 0 &&
         ![tab.url isEqualToString:@"about:blank"];
}

static std::int64_t Now() {
  return static_cast<std::int64_t>(NSDate.date.timeIntervalSince1970);
}

// ⌘D and the star: bookmarks the page at once (as Chrome does) and opens
// the bubble; on a bookmarked page the bubble edits that bookmark.
- (IBAction)bookmarkPage:(id)sender {
  BRTab* tab = [self activeTab];
  if (![self canBookmarkTab:tab]) {
    NSBeep();
    return;
  }
  BRBookmarkStore* store = [_owner bookmarkStore];
  brouk::ProfileId profile = tab.profileId;
  std::string url = tab.url.UTF8String ?: "";
  std::vector<brouk::BookmarkId> existing =
      store.model.BookmarksWithUrl(url, profile);
  brouk::BookmarkId bookmark = existing.empty() ? 0 : existing.front();
  BOOL added = NO;
  if (bookmark == 0) {
    brouk::BookmarkId folder = store.lastUsedFolder;
    if (!store.model.Find(folder) ||
        !store.model.IsVisibleIn(folder, profile)) {
      folder = brouk::BookmarkModel::kBarId;
    }
    std::string title = tab.title.length ? tab.title.UTF8String : url;
    [store change:[&](brouk::BookmarkModel& model) {
      // Linked to the tab's profile unless the user picks General (011).
      brouk::Result<brouk::BookmarkId> id = model.AddBookmark(
          folder, brouk::BookmarkModel::kEnd, title, url, profile, Now());
      if (id) {
        bookmark = id.value();
      }
    }];
    added = YES;
  }
  if (bookmark == 0) {
    NSBeep();
    return;
  }
  __weak BRBrowserWindowController* weakSelf = self;
  [BRBookmarkBubble showForBookmark:bookmark
                              store:store
                           profiles:[_owner profileList]
                      activeProfile:profile
                         newlyAdded:added
                         relativeTo:_starButton
                            removed:^{
                              [weakSelf offerUndoRemove:@"Bookmark removed"];
                            }];
}

// "Bookmark All Tabs…": the tabs of the active tab's profile group, in a
// new folder.
- (IBAction)bookmarkAllTabs:(id)sender {
  BRTab* active = [self activeTab];
  if (!active || _isPopup) {
    NSBeep();
    return;
  }
  brouk::ProfileId profile = active.profileId;
  NSMutableArray<BRTab*>* tabs = [NSMutableArray array];
  for (std::size_t i = 0; i < _model->count(); ++i) {
    BRTab* tab = _tabs[@(_model->id_at(i))];
    if (tab.profileId == profile && [self canBookmarkTab:tab]) {
      [tabs addObject:tab];
    }
  }
  if (tabs.count == 0) {
    NSBeep();
    return;
  }
  BRBookmarkStore* store = [_owner bookmarkStore];
  brouk::BookmarkId folder = store.lastUsedFolder;
  if (!store.model.Find(folder) || !store.model.IsVisibleIn(folder, profile)) {
    folder = brouk::BookmarkModel::kBarId;
  }
  std::vector<std::pair<std::string, std::string>> pages;
  for (BRTab* tab in tabs) {
    std::string url = tab.url.UTF8String ?: "";
    pages.emplace_back(tab.title.length ? tab.title.UTF8String : url, url);
  }
  [BRBookmarkEditor runKind:BookmarkEditorKind::kNewFolderWithParent
                      title:@"Bookmark All Tabs"
                     values:{.name = "",
                             .url = "",
                             .folder = folder,
                             .profile = profile}
                    editing:0
                      store:store
                   profiles:[_owner profileList]
              activeProfile:profile
                     window:self.window
                       done:^(const BookmarkEditorValues& values) {
                         [store change:[&](brouk::BookmarkModel& model) {
                           brouk::Result<brouk::BookmarkId> created =
                               model.AddFolder(
                                   values.folder, brouk::BookmarkModel::kEnd,
                                   values.name.empty() ? "Tabs" : values.name,
                                   values.profile, Now());
                           if (!created) {
                             return;
                           }
                           for (const auto& [title, url] : pages) {
                             model.AddBookmark(created.value(),
                                               brouk::BookmarkModel::kEnd,
                                               title, url, "", Now());
                           }
                         }];
                         store.lastUsedFolder = values.folder;
                       }];
}

// After a deletion (011: any deletion can be undone): a short notice with
// Undo, under the star.
- (void)offerUndoRemove:(NSString*)message {
  BRBookmarkStore* store = [_owner bookmarkStore];
  if (![store canUndoRemove]) {
    return;
  }
  NSView* anchor = _starButton.hidden ? _addressField : _starButton;
  [BRBookmarkUndoNotice showMessage:message store:store relativeTo:anchor];
}

- (void)openBookmark:(brouk::BookmarkId)bookmarkId
         disposition:(BRBookmarkOpenDisposition)disposition {
  BRBookmarkStore* store = [_owner bookmarkStore];
  const brouk::BookmarkItem* item = store.model.Find(bookmarkId);
  if (!item || item->folder) {
    return;
  }
  NSString* url = @(item->url.c_str());
  if (!url.length) {
    return;
  }
  BRTab* active = [self activeTab];
  brouk::ProfileId linked = store.model.EffectiveProfile(bookmarkId);
  brouk::ProfileId profile = !linked.empty() ? linked : [self activeProfileId];
  if (![_owner profileExists:profile]) {
    profile = [_owner defaultProfileId];
  }
  if (disposition == BRBookmarkOpenNewWindow) {
    [_owner openWindowWithURL:url profile:profile];
    return;
  }
  if (disposition == BRBookmarkOpenCurrentTab && active && !_isPopup &&
      active.profileId == profile) {
    active.nextVisitBookmark = YES;
    [self navigateTab:active toURL:url typed:NO];
    return;
  }
  BOOL activate = disposition != BRBookmarkOpenBackgroundTab;
  BRTab* tab = [self makeTabInProfile:profile];
  if (active && active.profileId == profile) {
    _model->InsertAfter(tab.tabId, active.tabId, activate);
  } else {
    _model->AppendTab(tab.tabId, profile, activate);
  }
  tab.nextVisitBookmark = YES;
  [self startTab:tab url:url];
  [self modelDidChange];
  if (activate) {
    [self focusActiveTab];
  }
}

- (BOOL)validateMenuItem:(NSMenuItem*)item {
  BRTab* tab = [self activeTab];
  SEL action = item.action;
  if (action == @selector(bookmarkPage:)) {
    return [self canBookmarkTab:tab];
  }
  if (action == @selector(bookmarkAllTabs:)) {
    return tab != nil && !_isPopup;
  }
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
  if (action == @selector(closeOtherTabs:)) {
    return [self activeTabCanClose:brouk::TabStripModel::CloseScope::kOthers];
  }
  if (action == @selector(closeTabsToTheLeft:)) {
    return [self activeTabCanClose:brouk::TabStripModel::CloseScope::kLeft];
  }
  if (action == @selector(closeTabsToTheRight:)) {
    return [self activeTabCanClose:brouk::TabStripModel::CloseScope::kRight];
  }
  if (action == @selector(newTabToTheRight:) ||
      action == @selector(duplicateTab:) ||
      action == @selector(showCommandPalette:)) {
    return tab != nil && !_isPopup;
  }
  if (action == @selector(toggleMuteTab:)) {
    item.title = tab.isMuted ? @"Unmute Tab" : @"Mute Tab";
    return tab != nil;
  }
  if (action == @selector(togglePinTab:)) {
    item.title = tab && _model->IsPinned(tab.tabId) ? @"Unpin Tab" : @"Pin Tab";
    return tab != nil;
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

- (void)windowDidMove:(NSNotification*)notification {
  [_owner browserWindowSessionChanged];
}

- (void)windowDidResize:(NSNotification*)notification {
  [_owner browserWindowSessionChanged];
}

- (void)windowWillClose:(NSNotification*)notification {
  if (_closingWindow && !_windowSnapshot.empty()) {
    [_owner browserWindow:self closedWindowWithTabs:std::move(_windowSnapshot)];
    _windowSnapshot.clear();
  }
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
