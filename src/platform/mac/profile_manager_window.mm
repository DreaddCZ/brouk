#import "platform/mac/profile_manager_window.h"

#include <memory>
#include <string>

#include "cef/internal_page_client.h"
#include "include/cef_browser.h"
#include "include/wrapper/cef_helpers.h"

@interface BRProfileManagerWindowController () <NSWindowDelegate>
- (void)handleBrowserCreated:(CefRefPtr<CefBrowser>)browser;
- (void)handleCloseReady;
- (void)handleBrowserClosed;
- (void)handleTitle:(NSString*)title;
- (BOOL)handleKeyEvent:(NSEvent*)event;
@end

namespace brouk {

// Forwards the page client's events to the window controller.
class ProfileManagerBridge : public InternalPageDelegate {
 public:
  explicit ProfileManagerBridge(BRProfileManagerWindowController* controller)
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
  __weak BRProfileManagerWindowController* controller_;
};

}  // namespace brouk

@implementation BRProfileManagerWindowController {
  __weak id<BRProfileManagerOwner> _owner;
  std::unique_ptr<brouk::ProfileManagerBridge> _bridge;
  CefRefPtr<brouk::InternalPageClient> _client;
  CefRefPtr<CefBrowser> _browser;
  BOOL _closing;
  BOOL _closeWhenCreated;
}

- (instancetype)initWithOwner:(id<BRProfileManagerOwner>)owner
                     delegate:(brouk::ProfileManagerDelegate*)delegate
                      context:(CefRefPtr<CefRequestContext>)context {
  CEF_REQUIRE_UI_THREAD();
  NSWindow* window = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(0, 0, 640, 600)
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                          NSWindowStyleMaskMiniaturizable |
                          NSWindowStyleMaskResizable
                  backing:NSBackingStoreBuffered
                    defer:NO];
  window.title = @"Profiles";
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
    _bridge = std::make_unique<brouk::ProfileManagerBridge>(self);
    _client = new brouk::InternalPageClient(
        _bridge.get(),
        std::make_unique<brouk::ProfileManagerHandler>(delegate));
    NSRect bounds = content.bounds;
    CefWindowInfo info;
    info.SetAsChild((__bridge void*)content,
                    CefRect(0, 0, static_cast<int>(bounds.size.width),
                            static_cast<int>(bounds.size.height)));
    info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
    CefBrowserSettings settings;
    CefBrowserHost::CreateBrowser(info, _client, "brouk://profiles/", settings,
                                  nullptr, context);
  }
  return self;
}

- (void)closeManager {
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
  [_owner profileManagerDidCreateBrowser];
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
  [_owner profileManagerDidClose:self];
}

- (void)handleTitle:(NSString*)title {
  self.window.title = title.length ? title : @"Profiles";
}

- (BOOL)handleKeyEvent:(NSEvent*)event {
  return [_owner profileManagerHandleKeyEvent:event];
}

// File > Close Tab (Cmd+W) closes this window.
- (IBAction)closeCurrentTab:(id)sender {
  [self closeManager];
}

#pragma mark - NSWindowDelegate

- (BOOL)windowShouldClose:(NSWindow*)sender {
  // Close the page first; the window goes when the browser has closed.
  if (_browser || !_closing) {
    [self closeManager];
    return NO;
  }
  return YES;
}

@end
