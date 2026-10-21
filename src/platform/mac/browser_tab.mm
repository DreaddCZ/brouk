#import "platform/mac/browser_tab.h"

#import "platform/mac/permission_prompt_view.h"
#import "platform/mac/sad_tab_view.h"
#import "platform/mac/tab_dialog_view.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "include/cef_browser.h"
#include "include/wrapper/cef_helpers.h"

namespace {

NSString* ToNSString(const std::string& s) {
  return [[NSString alloc] initWithBytes:s.data()
                                  length:s.size()
                                encoding:NSUTF8StringEncoding]
             ?: @"";
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

@implementation BRTab {
  std::unique_ptr<brouk::TabDelegateBridge> _bridge;
  CefRefPtr<brouk::TabClient> _client;
  CefRefPtr<CefBrowser> _browser;
  // Set when closing was requested before the browser existed.
  BOOL _closeWhenCreated;
  BRTabDialogView* _dialog;
  BRTabDialogView* _unresponsiveDialog;
  BRTabDialogView* _externalProtocolDialog;
  BRSadTabView* _sadTab;
  BOOL _clientDetached;
  // Permission prompts of this tab; the first one is shown.
  NSMutableArray<BRPermissionPromptView*>* _permissionPrompts;
}

- (instancetype)initWithId:(brouk::TabId)tabId
                  delegate:(id<BRTabDelegate>)delegate {
  if ((self = [super init])) {
    _tabId = tabId;
    _delegate = delegate;
    _title = @"";
    _url = @"";
    _findText = @"";
    _permissionPrompts = [NSMutableArray array];
    _containerView = [[NSView alloc] initWithFrame:NSZeroRect];
    _containerView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
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
  }
  [_delegate tabDidChangeState:self];
}

- (void)handleTitle:(NSString*)title {
  _title = [title copy];
  [_delegate tabDidChangeState:self];
}

- (void)handleAddress:(NSString*)url {
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
  if (!_containerView.hidden) {
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

// Chrome's "Page unresponsive" question, shown over the tab.
- (void)handleUnresponsive:(brouk::UnresponsiveDone)done {
  [_unresponsiveDialog dismiss];
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
  if (!_containerView.hidden) {
    [_unresponsiveDialog focus];
  }
}

- (void)handleResponsive {
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
  if (!_containerView.hidden) {
    [_dialog focus];
  }
}

- (void)dismissJsDialog {
  [_dialog dismiss];
  _dialog = nil;
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
  [_containerView removeFromSuperview];
}

- (void)handleBrowserClosed {
  _browser = nullptr;
  _client->DetachDelegate();
  _clientDetached = YES;
  // Release CEF objects while CEF is still running: releasing them after
  // CefShutdown is an error (CEF asserts in debug builds).
  _client = nullptr;
  [_delegate tabDidClose:self];
}

@end
