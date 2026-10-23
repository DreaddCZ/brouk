#import "platform/mac/browser_tab.h"

#import "platform/mac/devtools_dock.h"
#import "platform/mac/permission_prompt_view.h"
#import "platform/mac/sad_tab_view.h"
#import "platform/mac/screen_share_picker.h"
#import "platform/mac/tab_dialog_view.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/address_input.h"
#include "include/cef_browser.h"
#include "include/wrapper/cef_helpers.h"

namespace {

NSString* ToNSString(const std::string& s) {
  return [[NSString alloc] initWithBytes:s.data()
                                  length:s.size()
                                encoding:NSUTF8StringEncoding]
             ?: @"";
}

NSString* StripFragment(NSString* url) {
  NSRange hash = [url rangeOfString:@"#"];
  return hash.location == NSNotFound ? url
                                     : [url substringToIndex:hash.location];
}

}  // namespace

@interface BRTab ()
- (void)handleBrowserCreated:(CefRefPtr<CefBrowser>)browser;
- (void)handleTitle:(NSString*)title;
- (void)handleAddress:(NSString*)url;
- (void)handleLoading:(BOOL)loading back:(BOOL)back forward:(BOOL)forward;
- (void)handleFavicon:(NSImage*)image;
- (void)handleCloseReady;
- (void)handleBrowserClosed;
- (void)handlePopupBlocked:(NSString*)url;
- (void)handleAudible:(BOOL)audible;
- (void)chooseDesktopSource:(brouk::TabClientDelegate::DesktopSourceDone)done;
- (void)showJsDialog:(const brouk::JsDialogRequest&)request
                done:(brouk::JsDialogDone)done;
- (void)dismissJsDialog;
- (void)handleRendererGone:(brouk::RendererGoneReason)reason
                 errorCode:(NSString*)errorCode;
- (void)handleUnresponsive:(brouk::UnresponsiveDone)done;
- (void)handleResponsive;
- (void)handleFindResult:(int)count active:(int)active final:(BOOL)final;
- (void)showPermissionPrompt:(const brouk::PermissionRequest&)request
                        done:(brouk::PermissionDone)done;
- (void)dismissPermissionPrompt:(std::uint64_t)requestId;
- (void)askOpenExternalProtocol:(const brouk::ExternalProtocolRequest&)request
                           done:(brouk::ExternalProtocolDone)done;
@end

namespace brouk {

// Forwards TabClient callbacks (C++) to the BRTab (Objective-C). Holds the
// tab weakly: CEF may still call while the tab is being torn down.
class TabDelegateBridge : public TabClientDelegate {
 public:
  explicit TabDelegateBridge(BRTab* tab) : tab_(tab) {}

  void OnBrowserCreated(CefRefPtr<CefBrowser> browser) override {
    [tab_ handleBrowserCreated:browser];
  }
  void OnTitleChanged(const std::string& title) override {
    [tab_ handleTitle:ToNSString(title)];
  }
  void OnAddressChanged(const std::string& url) override {
    [tab_ handleAddress:ToNSString(url)];
  }
  void OnLoadingStateChanged(bool is_loading,
                             bool can_go_back,
                             bool can_go_forward) override {
    [tab_ handleLoading:is_loading back:can_go_back forward:can_go_forward];
  }
  void OnFaviconChanged(const std::vector<std::uint8_t>& png) override {
    NSImage* image = nil;
    if (!png.empty()) {
      NSData* data = [NSData dataWithBytes:png.data() length:png.size()];
      image = [[NSImage alloc] initWithData:data];
      // The PNG is at 2x; draw it at 16 pt.
      image.size = NSMakeSize(16, 16);
    }
    [tab_ handleFavicon:image];
  }
  std::optional<PopupHost> OnBeforePopup(const PopupRequest& request) override {
    BRTab* tab = tab_;
    if (!tab) {
      return std::nullopt;
    }
    return [tab.delegate tab:tab wantsPopup:request];
  }
  void OnPopupBlocked(const std::string& target_url) override {
    [tab_ handlePopupBlocked:ToNSString(target_url)];
  }
  void OnPopupAborted(int popup_id) override {
    BRTab* tab = tab_;
    [tab.delegate tab:tab popupAborted:popup_id];
  }
  void ShowJsDialog(const JsDialogRequest& request,
                    JsDialogDone done) override {
    [tab_ showJsDialog:request done:std::move(done)];
  }
  void DismissJsDialog() override { [tab_ dismissJsDialog]; }
  void OnRendererGone(RendererGoneReason reason,
                      const std::string& error_code) override {
    [tab_ handleRendererGone:reason errorCode:ToNSString(error_code)];
  }
  void OnRendererUnresponsive(UnresponsiveDone done) override {
    [tab_ handleUnresponsive:std::move(done)];
  }
  void OnRendererResponsive() override { [tab_ handleResponsive]; }
  void OpenUrl(const std::string& url, OpenDisposition disposition) override {
    BRTab* tab = tab_;
    [tab.delegate tab:tab openURL:ToNSString(url) disposition:disposition];
  }
  void CopyTextToClipboard(const std::string& text) override {
    NSPasteboard* pasteboard = NSPasteboard.generalPasteboard;
    [pasteboard clearContents];
    [pasteboard setString:ToNSString(text) forType:NSPasteboardTypeString];
  }
  void CopyImageToClipboard(const std::vector<std::uint8_t>& png) override {
    NSData* data = [NSData dataWithBytes:png.data() length:png.size()];
    NSPasteboard* pasteboard = NSPasteboard.generalPasteboard;
    [pasteboard clearContents];
    [pasteboard setData:data forType:NSPasteboardTypePNG];
  }
  SearchEngine CurrentSearchEngine() override {
    id<BRTabDelegate> delegate = tab_.delegate;
    // A C++ value from a message to nil is not constructed: check first.
    return delegate
               ? [delegate searchEngine]
               : SearchEngine{"Google", std::string(kDefaultSearchTemplate)};
  }
  void InspectElement(int x, int y) override {
    [tab_ inspectElementAt:NSMakePoint(x, y)];
  }
  bool AskWhereToSaveDownloads() override {
    return [tab_.delegate askWhereToSaveDownloads];
  }
  std::string ReserveDownloadPath(const std::string& suggested) override {
    BRTab* tab = tab_;
    return [tab.delegate tab:tab reserveDownloadPath:suggested];
  }
  void ReleaseDownloadPath(const std::string& path) override {
    BRTab* tab = tab_;
    [tab.delegate tab:tab releaseDownloadPath:path];
  }
  void OnDownloadUpdated(const DownloadInfo& info) override {
    BRTab* tab = tab_;
    [tab.delegate tab:tab downloadUpdated:info];
  }
  void ShowPermissionPrompt(const PermissionRequest& request,
                            PermissionDone done) override {
    [tab_ showPermissionPrompt:request done:std::move(done)];
  }
  void DismissPermissionPrompt(std::uint64_t id) override {
    [tab_ dismissPermissionPrompt:id];
  }
  void AskOpenExternalProtocol(const ExternalProtocolRequest& request,
                               ExternalProtocolDone done) override {
    [tab_ askOpenExternalProtocol:request done:std::move(done)];
  }
  void OpenExternalUrl(const std::string& url) override {
    NSURL* nsurl = [NSURL URLWithString:ToNSString(url)];
    if (nsurl) {
      [NSWorkspace.sharedWorkspace openURL:nsurl];
    }
  }
  void OnFullscreenModeChanged(bool fullscreen) override {
    BRTab* tab = tab_;
    [tab.delegate tab:tab fullscreenChanged:fullscreen];
  }
  void OnAudibleChanged(bool audible) override { [tab_ handleAudible:audible]; }
  void ChooseDesktopSource(DesktopSourceDone done) override {
    [tab_ chooseDesktopSource:std::move(done)];
  }
  void OnFindResult(int count, int active, bool final_update) override {
    [tab_ handleFindResult:count active:active final:final_update];
  }
  void OnCloseReady() override { [tab_ handleCloseReady]; }
  void OnBrowserClosed() override { [tab_ handleBrowserClosed]; }
  // Shortcuts act on key-down only; CEF also reports char and key-up events.
  bool OnUnhandledKeyEvent(const CefKeyEvent& event,
                           CefEventHandle os_event) override {
    BRTab* tab = tab_;
    NSEvent* ns_event = (__bridge NSEvent*)os_event;
    return IsKeyDown(event, ns_event) && [tab.delegate tab:tab
                                             handleUnhandledKeyEvent:ns_event];
  }

 private:
  static bool IsKeyDown(const CefKeyEvent& event, NSEvent* os_event) {
    return os_event && event.type == KEYEVENT_RAWKEYDOWN &&
           os_event.type == NSEventTypeKeyDown;
  }

  __weak BRTab* tab_;
};

}  // namespace brouk

// Calls `layoutHandler` whenever its size changes (window resizes).
@interface BRTabOuterView : NSView
@property(nonatomic, copy, nullable) void (^layoutHandler)(void);
@end

@implementation BRTabOuterView
- (void)resizeSubviewsWithOldSize:(NSSize)oldSize {
  if (_layoutHandler) {
    _layoutHandler();
  } else {
    [super resizeSubviewsWithOldSize:oldSize];
  }
}
@end

// The line between page and dock; dragging it resizes the dock.
@interface BRDockDivider : NSView
@property(nonatomic) BOOL vertical;  // A vertical line (dock left or right).
@property(nonatomic, copy, nullable) void (^dragged)(CGFloat delta);
@property(nonatomic, copy, nullable) void (^finished)(void);
@end

@implementation BRDockDivider
- (void)drawRect:(NSRect)dirtyRect {
  [NSColor.separatorColor setFill];
  NSRectFill(self.bounds);
}
- (void)resetCursorRects {
  [self addCursorRect:self.bounds
               cursor:_vertical ? NSCursor.resizeLeftRightCursor
                                : NSCursor.resizeUpDownCursor];
}
- (void)mouseDown:(NSEvent*)event {
  NSPoint last = [self.superview convertPoint:event.locationInWindow
                                     fromView:nil];
  while (true) {
    NSEvent* next =
        [self.window nextEventMatchingMask:NSEventMaskLeftMouseDragged |
                                           NSEventMaskLeftMouseUp];
    if (next.type == NSEventTypeLeftMouseUp) {
      break;
    }
    NSPoint p = [self.superview convertPoint:next.locationInWindow
                                    fromView:nil];
    if (_dragged) {
      _dragged(_vertical ? p.x - last.x : p.y - last.y);
    }
    last = p;
  }
  if (_finished) {
    _finished();
  }
}
@end

@interface BRTab () <BRDevToolsDockDelegate, NSWindowDelegate>
@end

@implementation BRTab {
  brouk::ProfileId _profileId;
  // Developer tools (006).
  BRDevToolsDock* _dock;
  BRDockDivider* _divider;
  NSWindow* _dockWindow;  // While undocked.
  NSString* _dockPosition;
  CGFloat _dockSize;
  BOOL _devToolsWhenCreated;
  NSPoint _inspectWhenCreated;
  std::unique_ptr<brouk::TabDelegateBridge> _bridge;
  CefRefPtr<brouk::TabClient> _client;
  CefRefPtr<CefBrowser> _browser;
  // Set when closing was requested before the browser existed.
  BOOL _closeWhenCreated;
  BRTabDialogView* _dialog;
  BRTabDialogView* _unresponsiveDialog;
  // An unresponsive report that arrived while a modal panel (e.g. the print
  // dialog, which blocks the page) was up; shown once the panel is gone.
  std::optional<brouk::UnresponsiveDone> _pendingUnresponsive;
  NSTimer* _pendingUnresponsiveTimer;
  NSDate* _modalPanelLastSeen;
  // Waiting for the screen sharing picker. Holds CEF objects, so it is
  // dropped when the browser closes.
  std::optional<brouk::TabClientDelegate::DesktopSourceDone>
      _pendingDesktopSource;
  BRTabDialogView* _externalProtocolDialog;
  BRSadTabView* _sadTab;
  BOOL _clientDetached;
  NSMutableArray<NSString*>* _blockedPopupURLs;
  // Permission prompts of this tab; the first one is shown.
  NSMutableArray<BRPermissionPromptView*>* _permissionPrompts;
}

- (instancetype)initWithId:(brouk::TabId)tabId
                   profile:(const brouk::ProfileId&)profileId
                  delegate:(id<BRTabDelegate>)delegate {
  if ((self = [super init])) {
    _tabId = tabId;
    _profileId = profileId;
    _delegate = delegate;
    _title = @"";
    _url = @"";
    _findText = @"";
    _permissionPrompts = [NSMutableArray array];
    _blockedPopupURLs = [NSMutableArray array];
    BRTabOuterView* outer = [[BRTabOuterView alloc] initWithFrame:NSZeroRect];
    outer.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    __weak BRTab* weakSelf = self;
    outer.layoutHandler = ^{
      [weakSelf layoutTabView];
    };
    _tabView = outer;
    _containerView = [[NSView alloc] initWithFrame:NSZeroRect];
    _containerView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [_tabView addSubview:_containerView];
    _dockPosition = @"bottom";
    _dockSize = 320;
    _inspectWhenCreated = NSMakePoint(NSNotFound, NSNotFound);
    _bridge = std::make_unique<brouk::TabDelegateBridge>(self);
    _client = new brouk::TabClient(_bridge.get());
  }
  return self;
}

- (void)dealloc {
  // Normally detached in handleBrowserClosed. A tab whose browser never
  // existed (aborted popup) is freed while CEF runs; after CefShutdown the
  // client must not be touched (no CEF UI thread any more).
  if (_client && !_clientDetached) {
    _client->DetachDelegate();
  }
}

- (const brouk::ProfileId&)profileId {
  return _profileId;
}

- (CefRefPtr<CefClient>)client {
  return _client;  // Null once the browser has closed.
}

- (CefRefPtr<CefBrowser>)browser {
  return _browser;
}

- (void)createBrowserWithURL:(NSString*)url
              requestContext:(CefRefPtr<CefRequestContext>)requestContext {
  CEF_REQUIRE_UI_THREAD();
  NSRect bounds = _containerView.bounds;
  CefWindowInfo windowInfo;
  windowInfo.SetAsChild((__bridge void*)_containerView,
                        CefRect(0, 0, static_cast<int>(bounds.size.width),
                                static_cast<int>(bounds.size.height)));
  windowInfo.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  CefBrowserSettings settings;
  _url = [url copy];
  CefBrowserHost::CreateBrowser(windowInfo, _client, url.UTF8String, settings,
                                nullptr, requestContext);
}

- (void)loadURL:(NSString*)url {
  _url = [url copy];
  if (_browser) {
    _browser->GetMainFrame()->LoadURL(url.UTF8String);
  }
}

- (void)goBack {
  if (_browser) {
    _browser->GoBack();
  }
}

- (void)goForward {
  if (_browser) {
    _browser->GoForward();
  }
}

- (void)reload {
  if (_browser) {
    _browser->Reload();
  }
}

- (void)reloadIgnoringCache {
  if (_browser) {
    _browser->ReloadIgnoreCache();
  }
}

- (void)stopLoading {
  if (_browser) {
    _browser->StopLoad();
  }
}

- (BOOL)hasDialog {
  return _dialog != nil && _dialog.superview != nil;
}

- (void)focusContent {
  if ([self hasDialog]) {
    [_dialog focus];
    return;
  }
  if (_browser) {
    _browser->GetHost()->SetFocus(true);
  }
}

- (void)findText:(NSString*)text forward:(BOOL)forward findNext:(BOOL)findNext {
  _findText = [text copy];
  if (!_browser) {
    return;
  }
  if (text.length == 0) {
    [self stopFinding];
    return;
  }
  _browser->GetHost()->Find(text.UTF8String, forward, /*matchCase=*/false,
                            findNext);
}

- (void)stopFinding {
  _findCount = 0;
  _findActiveMatch = 0;
  if (_browser) {
    _browser->GetHost()->StopFinding(/*clearSelection=*/true);
  }
}

- (void)handleFindResult:(int)count active:(int)active final:(BOOL)final {
  _findCount = count;
  _findActiveMatch = active;
  [_delegate tabDidChangeState:self];
}

- (void)zoom:(cef_zoom_command_t)command {
  if (_browser && _browser->GetHost()->CanZoom(command)) {
    _browser->GetHost()->Zoom(command);
    [_delegate tabDidChangeState:self];
  }
}

- (void)print {
  if (_browser) {
    _browser->GetHost()->Print();
  }
}

- (void)exitFullscreen {
  if (_browser && _browser->GetHost()->IsFullscreen()) {
    _browser->GetHost()->ExitFullscreen(/*will_cause_resize=*/true);
  }
}

- (double)zoomLevel {
  return _browser ? _browser->GetHost()->GetZoomLevel() : 0.0;
}

- (void)requestClose {
  _isClosing = YES;
  if (_browser) {
    _browser->GetHost()->CloseBrowser(false);
  } else {
    _closeWhenCreated = YES;
  }
}

- (void)forceClose {
  _isClosing = YES;
  if (_browser) {
    _browser->GetHost()->CloseBrowser(true);
  } else {
    _closeWhenCreated = YES;
  }
}

#pragma mark - Events from TabClient

- (void)handleBrowserCreated:(CefRefPtr<CefBrowser>)browser {
  _browser = browser;
  [_delegate tabDidCreateBrowser:self];
  // Let the browser view follow the container's size (window resizes).
  NSView* browserView = (__bridge NSView*)browser->GetHost()->GetWindowHandle();
  browserView.frame = _containerView.bounds;
  browserView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  if (_closeWhenCreated) {
    _browser->GetHost()->CloseBrowser(true);
  } else if (_devToolsWhenCreated) {
    // Dev tools were switched on (or inherited from the opener, 006)
    // before the page existed.
    _devToolsWhenCreated = NO;
    [self openDockInspecting:_inspectWhenCreated];
  }
  [_delegate tabDidChangeState:self];
}

- (void)handleTitle:(NSString*)title {
  _title = [title copy];
  [_delegate tabDidChangeState:self];
}

- (void)handleAddress:(NSString*)url {
  // Like Chrome, the list of blocked popups belongs to the page shown.
  if (![StripFragment(url) isEqualToString:StripFragment(_url)]) {
    [_blockedPopupURLs removeAllObjects];
  }
  _url = [url copy];
  [_delegate tabDidChangeState:self];
}

#pragma mark - External protocols

// Chrome's "Open <app>?" question. Links to schemes no app handles do
// nothing, as in Chrome.
- (void)askOpenExternalProtocol:(const brouk::ExternalProtocolRequest&)request
                           done:(brouk::ExternalProtocolDone)done {
  NSURL* url = [NSURL URLWithString:ToNSString(request.url)];
  NSURL* app =
      url ? [NSWorkspace.sharedWorkspace URLForApplicationToOpenURL:url] : nil;
  if (!app) {
    done(false, false);
    return;
  }
  NSString* appName = [[NSFileManager.defaultManager displayNameAtPath:app.path]
      stringByDeletingPathExtension];
  NSString* origin = ToNSString(request.origin);
  NSString* message =
      origin.length
          ? [NSString
                stringWithFormat:@"%@ wants to open this application.", origin]
          : @"A link wants to open this application.";
  NSString* checkbox =
      request.can_remember
          ? [NSString stringWithFormat:@"Always allow %@ to open %@: links "
                                       @"in the associated app",
                                       origin, ToNSString(request.scheme)]
          : nil;
  __block brouk::ExternalProtocolDone answer = std::move(done);
  [_externalProtocolDialog dismiss];
  _externalProtocolDialog = [[BRTabDialogView alloc]
      initWithTitle:[NSString stringWithFormat:@"Open %@?", appName]
            message:message
            okTitle:[NSString stringWithFormat:@"Open %@", appName]
        cancelTitle:@"Cancel"
         promptText:nil
      checkboxTitle:checkbox
         completion:^(BOOL ok, NSString*, BOOL always) {
           answer(ok, ok && always);
         }];
  _externalProtocolDialog.frame = _containerView.bounds;
  [_containerView addSubview:_externalProtocolDialog];
  if (!_tabView.isHiddenOrHasHiddenAncestor) {
    [_externalProtocolDialog focus];
  }
}

#pragma mark - Permission prompts

- (void)showPermissionPrompt:(const brouk::PermissionRequest&)request
                        done:(brouk::PermissionDone)done {
  __weak BRTab* weakSelf = self;
  BRPermissionPromptView* prompt = [[BRPermissionPromptView alloc]
      initWithRequest:request
                 done:[weakSelf, answer = std::move(done)](
                          brouk::PermissionChoice choice) {
                   answer(choice);
                   [weakSelf showNextPermissionPrompt];
                 }];
  [_permissionPrompts addObject:prompt];
  if (_permissionPrompts.count == 1) {
    [self layOutPermissionPrompt:prompt];
  }
}

// Places a prompt under the address bar, at the top left of the page.
- (void)layOutPermissionPrompt:(BRPermissionPromptView*)prompt {
  NSRect b = _containerView.bounds;
  NSRect f = prompt.frame;
  f.origin = NSMakePoint(100, b.size.height - f.size.height - 6);
  prompt.frame = f;
  prompt.autoresizingMask = NSViewMinYMargin | NSViewMaxXMargin;
  [_containerView addSubview:prompt];
}

- (void)showNextPermissionPrompt {
  NSIndexSet* gone =
      [_permissionPrompts indexesOfObjectsPassingTest:^BOOL(
                              BRPermissionPromptView* p, NSUInteger, BOOL*) {
        return p.superview == nil;
      }];
  [_permissionPrompts removeObjectsAtIndexes:gone];
  BRPermissionPromptView* next = _permissionPrompts.firstObject;
  if (next && !next.superview) {
    [self layOutPermissionPrompt:next];
  }
}

- (void)dismissPermissionPrompt:(std::uint64_t)requestId {
  for (BRPermissionPromptView* prompt in [_permissionPrompts copy]) {
    if (prompt.requestId == requestId) {
      [prompt dismiss];
    }
  }
  [self showNextPermissionPrompt];
}

- (void)handleRendererGone:(brouk::RendererGoneReason)reason
                 errorCode:(NSString*)errorCode {
  [self dismissJsDialog];
  [self handleResponsive];
  [_sadTab removeFromSuperview];
  NSString* message =
      reason == brouk::RendererGoneReason::kOutOfMemory
          ? @"The page ran out of memory while displaying this webpage."
          : @"Something went wrong while displaying this webpage.";
  __weak BRTab* weakSelf = self;
  _sadTab = [[BRSadTabView alloc] initWithMessage:message
                                        errorCode:errorCode
                                           reload:^{
                                             [weakSelf reload];
                                           }];
  _sadTab.frame = _containerView.bounds;
  [_containerView addSubview:_sadTab];
  _isLoading = NO;
  [_delegate tabDidChangeState:self];
}

// Main thread. True while a modal panel or sheet is up. The print dialog
// keeps the page busy until it closes, so the page then looks unresponsive;
// Chrome does not ask about it, and our question could not be answered
// anyway (it sits behind the panel).
- (BOOL)isBehindModalPanel {
  return NSApp.modalWindow != nil || _tabView.window.attachedSheet != nil;
}

- (void)cancelPendingUnresponsive {
  [_pendingUnresponsiveTimer invalidate];
  _pendingUnresponsiveTimer = nil;
  _pendingUnresponsive.reset();
}

// Main thread. Called by a timer while an unresponsive report waits for a
// modal panel to close.
- (void)checkPendingUnresponsive {
  if (!_pendingUnresponsive) {
    return;
  }
  if ([self isBehindModalPanel]) {
    _modalPanelLastSeen = [NSDate date];
    return;
  }
  // The page needs a moment after the panel closes to answer again.
  if (-_modalPanelLastSeen.timeIntervalSinceNow < 2) {
    return;
  }
  brouk::UnresponsiveDone done = std::move(*_pendingUnresponsive);
  [self cancelPendingUnresponsive];
  [self handleUnresponsive:std::move(done)];
}

// Chrome's "Page unresponsive" question, shown over the tab.
- (void)handleUnresponsive:(brouk::UnresponsiveDone)done {
  CEF_REQUIRE_UI_THREAD();
  [_unresponsiveDialog dismiss];
  _unresponsiveDialog = nil;
  if ([self isBehindModalPanel]) {
    // Ask only if the page is still unresponsive after the panel closes
    // (handleResponsive cancels it). The timer also runs in the modal run
    // loop mode, but does nothing until the panel is gone.
    [self cancelPendingUnresponsive];
    _pendingUnresponsive = std::move(done);
    _modalPanelLastSeen = [NSDate date];
    __weak BRTab* weakSelf = self;
    _pendingUnresponsiveTimer =
        [NSTimer timerWithTimeInterval:0.5
                               repeats:YES
                                 block:^(NSTimer*) {
                                   [weakSelf checkPendingUnresponsive];
                                 }];
    [NSRunLoop.mainRunLoop addTimer:_pendingUnresponsiveTimer
                            forMode:NSRunLoopCommonModes];
    return;
  }
  NSString* name = _title.length ? _title : _url;
  __block brouk::UnresponsiveDone answer = std::move(done);
  __weak BRTab* weakSelf = self;
  _unresponsiveDialog = [[BRTabDialogView alloc]
      initWithTitle:@"Page unresponsive"
            message:[NSString stringWithFormat:
                                  @"“%@” is not responding. You can wait "
                                  @"for it to become responsive or exit "
                                  @"the page.",
                                  name]
            okTitle:@"Wait"
        cancelTitle:@"Exit page"
         promptText:nil
      checkboxTitle:nil
         completion:^(BOOL ok, NSString*, BOOL) {
           BRTab* strongSelf = weakSelf;
           if (strongSelf) {
             strongSelf->_unresponsiveDialog = nil;
           }
           answer(/*exit_page=*/!ok);
         }];
  _unresponsiveDialog.frame = _containerView.bounds;
  [_containerView addSubview:_unresponsiveDialog];
  if (!_tabView.isHiddenOrHasHiddenAncestor) {
    [_unresponsiveDialog focus];
  }
}

- (void)handleResponsive {
  CEF_REQUIRE_UI_THREAD();
  [self cancelPendingUnresponsive];
  [_unresponsiveDialog dismiss];
  _unresponsiveDialog = nil;
}

- (void)handleLoading:(BOOL)loading back:(BOOL)back forward:(BOOL)forward {
  if (loading && _sadTab) {
    // A new load (e.g. Reload) replaces the crash page.
    [_sadTab removeFromSuperview];
    _sadTab = nil;
  }
  _isLoading = loading;
  _canGoBack = back;
  _canGoForward = forward;
  if (!loading && _title.length == 0) {
    _title = _url;
  }
  [_delegate tabDidChangeState:self];
}

- (void)handleFavicon:(NSImage*)image {
  _favicon = image;
  [_delegate tabDidChangeState:self];
}

- (void)showJsDialog:(const brouk::JsDialogRequest&)request
                done:(brouk::JsDialogDone)done {
  [_dialog dismiss];
  _dialog = [BRTabDialogView dialogForRequest:request done:std::move(done)];
  _dialog.frame = _containerView.bounds;
  [_containerView addSubview:_dialog];
  // Only take focus if this tab is visible; otherwise the dialog waits until
  // the user switches to the tab (Chrome does not steal focus either).
  if (!_tabView.isHiddenOrHasHiddenAncestor) {
    [_dialog focus];
  }
}

- (void)dismissJsDialog {
  [_dialog dismiss];
  _dialog = nil;
}

// The page asked to share a screen or window (getDisplayMedia).
- (void)chooseDesktopSource:(brouk::TabClientDelegate::DesktopSourceDone)done {
  CEF_REQUIRE_UI_THREAD();
  // Only the tab the user is looking at may open the picker.
  if (_pendingDesktopSource || _tabView.isHiddenOrHasHiddenAncestor ||
      !_tabView.window.isVisible) {
    done(std::nullopt);
    return;
  }
  _pendingDesktopSource = std::move(done);
  __weak BRTab* weakSelf = self;
  [BRScreenSharePicker
      pickWithCompletion:^(std::optional<brouk::DesktopSource> source) {
        BRTab* strongSelf = weakSelf;
        if (!strongSelf || !strongSelf->_pendingDesktopSource) {
          return;  // The tab closed meanwhile.
        }
        brouk::TabClientDelegate::DesktopSourceDone answer =
            std::move(*strongSelf->_pendingDesktopSource);
        strongSelf->_pendingDesktopSource.reset();
        // The system picker takes the focus; give it back to the tab's
        // window, where the user continues.
        [NSApp activateIgnoringOtherApps:YES];
        [strongSelf->_tabView.window makeKeyAndOrderFront:nil];
        answer(source);
      }];
}

- (void)handleAudible:(BOOL)audible {
  _isAudible = audible;
  [_delegate tabDidChangeState:self];
}

- (BOOL)isMuted {
  return _browser && _browser->GetHost()->IsAudioMuted();
}

- (void)toggleMute {
  if (_browser) {
    _browser->GetHost()->SetAudioMuted(!_browser->GetHost()->IsAudioMuted());
    [_delegate tabDidChangeState:self];
  }
}

- (void)handlePopupBlocked:(NSString*)url {
  constexpr NSUInteger kMaxBlockedPopups = 20;
  if (_blockedPopupURLs.count < kMaxBlockedPopups) {
    [_blockedPopupURLs addObject:url];
  }
  [_delegate tabDidChangeState:self];
}

- (NSArray<NSString*>*)blockedPopupURLs {
  return [_blockedPopupURLs copy];
}

- (void)clearBlockedPopups {
  [_blockedPopupURLs removeAllObjects];
  [_delegate tabDidChangeState:self];
}

- (void)handleCloseReady {
  [self dismissJsDialog];
  // The page may close itself (window.close()) without a request from us.
  _isClosing = YES;
  [_delegate tabCloseReady:self];
  // Tearing down the view hierarchy completes the close (see DoClose in
  // cef_life_span_handler.h); CEF then calls OnBeforeClose.
  if (_browser) {
    NSView* browserView =
        (__bridge NSView*)_browser->GetHost()->GetWindowHandle();
    [browserView removeFromSuperview];
  }
  [_tabView removeFromSuperview];
}

- (void)sendRendererConfigWithJsonViewer:(BOOL)jsonViewer {
  if (_browser) {
    brouk::TabClient::SendRendererConfig(_browser, jsonViewer);
  }
}

#pragma mark - Developer tools (006)

- (void)setDevToolsOn:(BOOL)on {
  CEF_REQUIRE_UI_THREAD();
  if (on == _devToolsOn) {
    return;
  }
  _devToolsOn = on;
  if (on) {
    [self openDockInspecting:NSMakePoint(NSNotFound, NSNotFound)];
  } else {
    _devToolsWhenCreated = NO;
    [_dock close];  // devToolsDockDidClose: finishes.
  }
  [_delegate tabDevToolsChanged:self];
}

- (void)inspectElementAt:(NSPoint)point {
  if (_dock) {
    [_dock inspectAt:point];
    return;
  }
  _devToolsOn = YES;
  [self openDockInspecting:point];
  [_delegate tabDevToolsChanged:self];
}

- (void)showDevToolsConsole {
  [self setDevToolsOn:YES];
  [_dock showPanel:@"console"];
}

- (void)startDevToolsElementPicker {
  [self setDevToolsOn:YES];
  [_dock enterInspectElementMode];
}

- (void)openDockInspecting:(NSPoint)point {
  if (_dock) {
    return;
  }
  if (!_browser) {
    _devToolsWhenCreated = YES;
    _inspectWhenCreated = point;
    return;
  }
  _dockPosition = [_delegate dockPosition] ?: @"bottom";
  _dockSize = [_delegate dockSize];
  _dock = [[BRDevToolsDock alloc]
      initWithInspectedBrowser:_browser
                       inspect:point
                       context:[_delegate devToolsContext]
                   preferences:[_delegate devToolsPreferences]
                       counter:[_delegate browserCounter]
                      delegate:self];
  _dock.position = _dockPosition;
  [self placeDock];
}

- (void)setDockPosition:(NSString*)position size:(CGFloat)size {
  BOOL moved = ![position isEqualToString:_dockPosition];
  _dockPosition = [position copy];
  _dockSize = size;
  if (!_dock) {
    return;
  }
  _dock.position = position;
  if (moved) {
    [self placeDock];
  } else {
    [self layoutTabView];
  }
}

// Puts the dock into the tab (with a divider) or into its own window.
- (void)placeDock {
  if ([_dockPosition isEqualToString:@"window"]) {
    [_divider removeFromSuperview];
    _divider = nil;
    if (!_dockWindow) {
      _dockWindow = [[NSWindow alloc]
          initWithContentRect:NSMakeRect(0, 0, 900, 600)
                    styleMask:NSWindowStyleMaskTitled |
                              NSWindowStyleMaskClosable |
                              NSWindowStyleMaskMiniaturizable |
                              NSWindowStyleMaskResizable
                      backing:NSBackingStoreBuffered
                        defer:NO];
      _dockWindow.releasedWhenClosed = NO;
      _dockWindow.restorable = NO;
      _dockWindow.delegate = self;
      [_dockWindow
          cascadeTopLeftFromPoint:NSMakePoint(
                                      NSMinX(_tabView.window.frame) + 60,
                                      NSMaxY(_tabView.window.frame) - 60)];
    }
    _dockWindow.title =
        [NSString stringWithFormat:@"Developer Tools – %@",
                                   _title.length ? _title : _url];
    _dock.view.frame = _dockWindow.contentView.bounds;
    _dock.view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [_dockWindow.contentView addSubview:_dock.view];
    [_dockWindow makeKeyAndOrderFront:nil];
  } else {
    if (_dockWindow) {
      _dockWindow.delegate = nil;
      [_dockWindow orderOut:nil];
      [_dockWindow close];
      _dockWindow = nil;
    }
    _dock.view.autoresizingMask = NSViewNotSizable;
    [_tabView addSubview:_dock.view];
    if (!_divider) {
      _divider = [[BRDockDivider alloc] initWithFrame:NSZeroRect];
      __weak BRTab* weakSelf = self;
      _divider.dragged = ^(CGFloat delta) {
        [weakSelf dragDividerBy:delta];
      };
      _divider.finished = ^{
        BRTab* strongSelf = weakSelf;
        if (strongSelf) {
          [strongSelf->_delegate tab:strongSelf
                       resizedDockTo:strongSelf->_dockSize];
        }
      };
      [_tabView addSubview:_divider];
    }
    _divider.vertical = ![_dockPosition isEqualToString:@"bottom"];
    [_divider.window invalidateCursorRectsForView:_divider];
  }
  [self layoutTabView];
}

- (void)dragDividerBy:(CGFloat)delta {
  // The tab view is not flipped: y grows upwards.
  if ([_dockPosition isEqualToString:@"bottom"]) {
    _dockSize += delta;
  } else if ([_dockPosition isEqualToString:@"right"]) {
    _dockSize -= delta;
  } else {
    _dockSize += delta;
  }
  [self layoutTabView];
}

// Page and dock side by side (or the page alone).
- (void)layoutTabView {
  NSRect bounds = _tabView.bounds;
  BOOL docked = _dock && ![_dockPosition isEqualToString:@"window"];
  if (!docked) {
    _containerView.frame = bounds;
    return;
  }
  constexpr CGFloat kMin = 120;
  constexpr CGFloat kDivider = 1;
  BOOL bottom = [_dockPosition isEqualToString:@"bottom"];
  CGFloat total = bottom ? bounds.size.height : bounds.size.width;
  _dockSize =
      std::clamp<CGFloat>(_dockSize, kMin, std::max(kMin, total - kMin));
  CGFloat size = std::min(_dockSize, total);
  NSRect page = bounds;
  NSRect dock = bounds;
  NSRect line = bounds;
  if (bottom) {
    dock.size.height = size;
    line = NSMakeRect(0, size, bounds.size.width, kDivider);
    page.origin.y = size + kDivider;
    page.size.height = std::max<CGFloat>(0, total - size - kDivider);
  } else if ([_dockPosition isEqualToString:@"right"]) {
    dock.origin.x = total - size;
    dock.size.width = size;
    line = NSMakeRect(total - size - kDivider, 0, kDivider, bounds.size.height);
    page.size.width = std::max<CGFloat>(0, total - size - kDivider);
  } else {
    dock.size.width = size;
    line = NSMakeRect(size, 0, kDivider, bounds.size.height);
    page.origin.x = size + kDivider;
    page.size.width = std::max<CGFloat>(0, total - size - kDivider);
  }
  _containerView.frame = page;
  _dock.view.frame = dock;
  // A wider area than the line itself is easier to grab.
  _divider.frame = bottom ? NSInsetRect(line, 0, -2) : NSInsetRect(line, -2, 0);
}

#pragma mark - BRDevToolsDockDelegate

- (void)devToolsDockDidClose:(BRDevToolsDock*)dock {
  if (dock != _dock) {
    return;
  }
  [_dock.view removeFromSuperview];
  [_dock detach];
  _dock = nil;
  [_divider removeFromSuperview];
  _divider = nil;
  if (_dockWindow) {
    _dockWindow.delegate = nil;
    [_dockWindow close];
    _dockWindow = nil;
  }
  [self layoutTabView];
  if (_devToolsOn) {
    _devToolsOn = NO;
    [_delegate tabDevToolsChanged:self];
  }
}

- (void)devToolsDock:(BRDevToolsDock*)dock wantsPosition:(NSString*)position {
  [_delegate tab:self wantsDockPosition:position];
}

- (BOOL)devToolsDockHandleKeyEvent:(NSEvent*)event {
  return [_delegate tab:self handleUnhandledKeyEvent:event];
}

- (void)devToolsDock:(BRDevToolsDock*)dock openURL:(NSString*)url {
  // A link in DevTools opens next to the tab, in its profile.
  [_delegate tab:self
          openURL:url
      disposition:brouk::OpenDisposition::kNewForegroundTab];
}

#pragma mark - NSWindowDelegate (the undocked DevTools window)

- (BOOL)windowShouldClose:(NSWindow*)sender {
  // Closing the window turns the tab's dev tools off (006).
  [self setDevToolsOn:NO];
  return NO;  // Closed once DevTools has closed.
}

- (void)handleBrowserClosed {
  _browser = nullptr;
  // CEF closes the tab's DevTools with it; drop its objects now.
  [_dock detach];
  _dock = nil;
  [_dockWindow close];
  _dockWindow = nil;
  [self cancelPendingUnresponsive];  // Holds a CEF callback.
  _pendingDesktopSource.reset();
  _client->DetachDelegate();
  _clientDetached = YES;
  // Release CEF objects while CEF is still running: releasing them after
  // CefShutdown is an error (CEF asserts in debug builds).
  _client = nullptr;
  [_delegate tabDidClose:self];
}

@end
