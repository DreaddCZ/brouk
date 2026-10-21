#import "platform/mac/browser_tab.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
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
}

- (instancetype)initWithId:(brouk::TabId)tabId
                  delegate:(id<BRTabDelegate>)delegate {
  if ((self = [super init])) {
    _tabId = tabId;
    _delegate = delegate;
    _title = @"";
    _url = @"";
    _containerView = [[NSView alloc] initWithFrame:NSZeroRect];
    _containerView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    _bridge = std::make_unique<brouk::TabDelegateBridge>(self);
    _client = new brouk::TabClient(_bridge.get());
  }
  return self;
}

- (void)dealloc {
  if (_client) {
    _client->DetachDelegate();
  }
}

- (CefRefPtr<CefClient>)client {
  return _client;
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

- (void)focusContent {
  if (_browser) {
    _browser->GetHost()->SetFocus(true);
  }
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

- (void)handleLoading:(BOOL)loading back:(BOOL)back forward:(BOOL)forward {
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

- (void)handleCloseReady {
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
  [_delegate tabDidClose:self];
}

@end
