#import "platform/mac/internal_page_window.h"

#include <memory>
#include <string>
#include <utility>

#include "cef/internal_page_client.h"
#include "include/cef_browser.h"
#include "include/wrapper/cef_helpers.h"

@interface BRInternalPageWindowController () <NSWindowDelegate>
- (void)handleBrowserCreated:(CefRefPtr<CefBrowser>)browser;
- (void)handleCloseReady;
- (void)handleBrowserClosed;
- (void)handleTitle:(NSString*)title;
- (BOOL)handleKeyEvent:(NSEvent*)event;
@end

namespace brouk {

// Forwards the page client's events to the window controller.
class InternalPageBridge : public InternalPageDelegate {
 public:
  explicit InternalPageBridge(BRInternalPageWindowController* controller)
      : controller_(controller) {}

  void OnBrowserCreated(CefRefPtr<CefBrowser> browser) override {
    [controller_ handleBrowserCreated:browser];
  }
  void OnCloseReady() override { [controller_ handleCloseReady]; }
  void OnBrowserClosed() override { [controller_ handleBrowserClosed]; }
  void OnTitleChanged(const std::string& title) override {
    [controller_ handleTitle:@(title.c_str())];
  }
  bool OnUnhandledKeyEvent(const CefKeyEvent& event,
                           CefEventHandle os_event) override {
    NSEvent* ns_event = (__bridge NSEvent*)os_event;
    return ns_event && event.type == KEYEVENT_RAWKEYDOWN &&
           ns_event.type == NSEventTypeKeyDown &&
           [controller_ handleKeyEvent:ns_event];
  }

 private:
  __weak BRInternalPageWindowController* controller_;
};

}  // namespace brouk

@implementation BRInternalPageWindowController {
  __weak id<BRInternalPageOwner> _owner;
  std::unique_ptr<brouk::InternalPageBridge> _bridge;
  CefRefPtr<brouk::InternalPageClient> _client;
  CefRefPtr<CefBrowser> _browser;
  BOOL _closing;
  BOOL _closeWhenCreated;
  NSString* _defaultTitle;
}

- (instancetype)
    initWithOwner:(id<BRInternalPageOwner>)owner
              url:(NSString*)url
            title:(NSString*)title
             size:(NSSize)size
          handler:(std::unique_ptr<CefMessageRouterBrowserSide::Handler>)handler
          context:(CefRefPtr<CefRequestContext>)context {
  CEF_REQUIRE_UI_THREAD();
  NSWindow* window = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(0, 0, size.width, size.height)
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                          NSWindowStyleMaskMiniaturizable |
                          NSWindowStyleMaskResizable
                  backing:NSBackingStoreBuffered
                    defer:NO];
  window.title = title;
  _defaultTitle = [title copy];
  window.minSize = NSMakeSize(420, 360);
  window.releasedWhenClosed = NO;
  window.restorable = NO;
  window.tabbingMode = NSWindowTabbingModeDisallowed;
  [window center];
  if ((self = [super initWithWindow:window])) {
    _owner = owner;
    window.delegate = self;
    NSView* content = window.contentView;
    content.wantsLayer = YES;
    _bridge = std::make_unique<brouk::InternalPageBridge>(self);
    _client = new brouk::InternalPageClient(_bridge.get(), std::move(handler));
    NSRect bounds = content.bounds;
    CefWindowInfo info;
    info.SetAsChild((__bridge void*)content,
                    CefRect(0, 0, static_cast<int>(bounds.size.width),
                            static_cast<int>(bounds.size.height)));
    info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
    CefBrowserSettings settings;
    CefBrowserHost::CreateBrowser(info, _client, url.UTF8String, settings,
                                  nullptr, context);
  }
  return self;
}

- (void)closePage {
  if (_closing) {
    return;
  }
  _closing = YES;
  if (_browser) {
    _browser->GetHost()->CloseBrowser(false);
  } else {
    _closeWhenCreated = YES;
  }
}

#pragma mark - Page events (UI thread)

- (void)handleBrowserCreated:(CefRefPtr<CefBrowser>)browser {
  _browser = browser;
  // The browser view follows the window's size.
  NSView* view = (__bridge NSView*)browser->GetHost()->GetWindowHandle();
  view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  [_owner internalPageDidCreateBrowser];
  if (_closeWhenCreated) {
    browser->GetHost()->CloseBrowser(false);
  }
}

- (void)handleCloseReady {
  _closing = YES;
  if (_browser) {
    NSView* view = (__bridge NSView*)_browser->GetHost()->GetWindowHandle();
    [view removeFromSuperview];
  }
  [self.window orderOut:nil];
}

- (void)handleBrowserClosed {
  _browser = nullptr;
  _client->DetachDelegate();
  // Release CEF objects while CEF is running (see app shutdown).
  _client = nullptr;
  [self.window close];
  [_owner internalPageDidClose:self];
}

- (void)handleTitle:(NSString*)title {
  self.window.title = title.length ? title : _defaultTitle;
}

- (BOOL)handleKeyEvent:(NSEvent*)event {
  return [_owner internalPageHandleKeyEvent:event];
}

// File > Close Tab (Cmd+W) closes this window.
- (IBAction)closeCurrentTab:(id)sender {
  [self closePage];
}

#pragma mark - NSWindowDelegate

- (BOOL)windowShouldClose:(NSWindow*)sender {
  // Close the page first; the window goes when the browser has closed.
  if (_browser || !_closing) {
    [self closePage];
    return NO;
  }
  return YES;
}

@end
