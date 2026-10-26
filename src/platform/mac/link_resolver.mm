#import "platform/mac/link_resolver.h"

#include <string>
#include <utility>
#include <vector>

#include "cef/link_resolver_client.h"
#include "include/cef_browser.h"
#include "include/cef_request_context.h"
#include "include/cef_request_context_handler.h"
#include "include/wrapper/cef_helpers.h"
#import "platform/mac/data_paths.h"
#import "platform/mac/devtools_dock.h"

namespace {

// How long a wrapper may stay on its own page before the user sees it.
constexpr NSTimeInterval kShowAfter = 5.0;

}  // namespace

@class BRLinkResolution;

namespace brouk {

class LinkResolutionBridge : public LinkResolverDelegate {
 public:
  explicit LinkResolutionBridge(BRLinkResolution* resolution)
      : resolution_(resolution) {}
  void OnBrowserCreated(CefRefPtr<CefBrowser> browser) override;
  void OnResolved(const std::string& url) override;
  void OnCloseReady() override;
  void OnBrowserClosed() override;
  void OnTitleChanged(const std::string& title) override;

 private:
  __weak BRLinkResolution* resolution_;
};

}  // namespace brouk

// One opaque link being followed, with its own context, browser and
// (hidden until needed) "Checking link" window.
@interface BRLinkResolution : NSObject <NSWindowDelegate>
- (instancetype)initWithURL:(NSString*)url
                    wrapper:(NSString*)wrapperName
               userWrappers:(std::vector<brouk::LinkWrapperRule>)userWrappers
                     screen:(nullable NSScreen*)screen
                    counter:(id<BRBrowserCounter>)counter
                       done:(void (^)(NSString* _Nullable finalURL))done
                   finished:(void (^)(BRLinkResolution* resolution))finished;
- (void)cancel;
- (void)handleBrowserCreated:(CefRefPtr<CefBrowser>)browser;
- (void)handleResolved:(NSString*)url;
- (void)handleCloseReady;
- (void)handleBrowserClosed;
- (void)handleTitle:(NSString*)title;
@end

namespace brouk {

void LinkResolutionBridge::OnBrowserCreated(CefRefPtr<CefBrowser> browser) {
  [resolution_ handleBrowserCreated:browser];
}
void LinkResolutionBridge::OnResolved(const std::string& url) {
  [resolution_ handleResolved:@(url.c_str())];
}
void LinkResolutionBridge::OnCloseReady() {
  [resolution_ handleCloseReady];
}
void LinkResolutionBridge::OnBrowserClosed() {
  [resolution_ handleBrowserClosed];
}
void LinkResolutionBridge::OnTitleChanged(const std::string& title) {
  [resolution_ handleTitle:@(title.c_str())];
}

}  // namespace brouk

@implementation BRLinkResolution {
  NSWindow* _window;
  NSString* _wrapperName;
  NSString* _dataFolder;
  NSScreen* _screen;
  __weak id<BRBrowserCounter> _counter;
  std::unique_ptr<brouk::LinkResolutionBridge> _bridge;
  CefRefPtr<brouk::LinkResolverClient> _client;
  CefRefPtr<CefBrowser> _browser;
  NSTimer* _showTimer;
  void (^_done)(NSString* _Nullable);
  void (^_finished)(BRLinkResolution*);
  BOOL _closing;
}

- (instancetype)initWithURL:(NSString*)url
                    wrapper:(NSString*)wrapperName
               userWrappers:(std::vector<brouk::LinkWrapperRule>)userWrappers
                     screen:(nullable NSScreen*)screen
                    counter:(id<BRBrowserCounter>)counter
                       done:(void (^)(NSString* _Nullable finalURL))done
                   finished:(void (^)(BRLinkResolution* resolution))finished {
  CEF_REQUIRE_UI_THREAD();
  if ((self = [super init])) {
    _wrapperName = [wrapperName copy];
    _screen = screen;
    _counter = counter;
    _done = [done copy];
    _finished = [finished copy];
    _window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 820, 600)
                                          styleMask:NSWindowStyleMaskTitled |
                                                    NSWindowStyleMaskClosable |
                                                    NSWindowStyleMaskResizable
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    _window.title = [self windowTitle:nil];
    _window.releasedWhenClosed = NO;
    _window.restorable = NO;
    _window.tabbingMode = NSWindowTabbingModeDisallowed;
    _window.delegate = self;
    _window.contentView.wantsLayer = YES;

    // Its own folder, deleted afterwards: nothing it stores outlives this
    // resolution, and it shares nothing with any profile. (An in-memory
    // context never starts loading under the Chrome bootstrap.)
    CefRequestContextSettings contextSettings;
    _dataFolder = [brouk::mac::WebDataRootDirectory()
        stringByAppendingPathComponent:
            [@"link-check-" stringByAppendingString:NSUUID.UUID.UUIDString]];
    CefString(&contextSettings.cache_path) = _dataFolder.UTF8String;
    CefRefPtr<CefRequestContext> context =
        CefRequestContext::CreateContext(contextSettings, nullptr);
    _bridge = std::make_unique<brouk::LinkResolutionBridge>(self);
    _client =
        new brouk::LinkResolverClient(_bridge.get(), std::move(userWrappers));
    NSRect bounds = _window.contentView.bounds;
    CefWindowInfo info;
    info.SetAsChild((__bridge void*)_window.contentView,
                    CefRect(0, 0, static_cast<int>(bounds.size.width),
                            static_cast<int>(bounds.size.height)));
    info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
    CefBrowserSettings settings;
    CefBrowserHost::CreateBrowser(info, _client, url.UTF8String, settings,
                                  nullptr, context);
    __weak BRLinkResolution* weakSelf = self;
    _showTimer = [NSTimer scheduledTimerWithTimeInterval:kShowAfter
                                                 repeats:NO
                                                   block:^(NSTimer* /*t*/) {
                                                     [weakSelf showWindow];
                                                   }];
  }
  return self;
}

- (NSString*)windowTitle:(nullable NSString*)pageTitle {
  NSString* base =
      _wrapperName.length
          ? [NSString stringWithFormat:@"Checking link · via %@", _wrapperName]
          : @"Checking link";
  return pageTitle.length
             ? [NSString stringWithFormat:@"%@ — %@", base, pageTitle]
             : base;
}

// The wrapper has a page of its own: the user decides.
- (void)showWindow {
  _showTimer = nil;
  if (_closing) {
    return;
  }
  NSScreen* screen = _screen ?: NSScreen.mainScreen;
  NSRect area = screen.visibleFrame;
  NSRect frame = _window.frame;
  frame.origin = NSMakePoint(NSMidX(area) - frame.size.width / 2,
                             NSMidY(area) - frame.size.height / 2);
  [_window setFrame:frame display:NO];
  [NSApp activateIgnoringOtherApps:YES];
  [_window makeKeyAndOrderFront:nil];
}

- (void)finishWith:(nullable NSString*)url {
  void (^done)(NSString* _Nullable) = _done;
  _done = nil;
  if (done) {
    done(url);
  }
  [self cancel];
}

- (void)cancel {
  [_showTimer invalidate];
  _showTimer = nil;
  void (^done)(NSString* _Nullable) = _done;
  _done = nil;
  if (done) {
    done(nil);
  }
  if (_closing) {
    return;
  }
  _closing = YES;
  [_window orderOut:nil];
  if (_browser) {
    _browser->GetHost()->CloseBrowser(true);
  }
}

#pragma mark - Browser events (UI thread)

- (void)handleBrowserCreated:(CefRefPtr<CefBrowser>)browser {
  _browser = browser;
  [_counter browserWindowDidCreateBrowser];
  NSView* view = (__bridge NSView*)browser->GetHost()->GetWindowHandle();
  view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  if (_closing) {
    browser->GetHost()->CloseBrowser(true);
  }
}

- (void)handleResolved:(NSString*)url {
  // Called from inside a navigation, where the browser must not close.
  __weak BRLinkResolution* weakSelf = self;
  dispatch_async(dispatch_get_main_queue(), ^{
    [weakSelf finishWith:url];
  });
}

- (void)handleCloseReady {
  if (_browser) {
    NSView* view = (__bridge NSView*)_browser->GetHost()->GetWindowHandle();
    [view removeFromSuperview];
  }
}

- (void)handleBrowserClosed {
  BOOL counted = _browser != nullptr;
  _browser = nullptr;
  _client->DetachDelegate();
  // The context's data goes with it (once Chromium has let go of it).
  NSString* folder = _dataFolder;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC),
                 dispatch_get_main_queue(), ^{
                   [NSFileManager.defaultManager removeItemAtPath:folder
                                                            error:nil];
                 });
  [_window close];
  if (counted) {
    [_counter browserWindowDidCloseBrowser];
  }
  void (^finished)(BRLinkResolution*) = _finished;
  _finished = nil;
  if (finished) {
    finished(self);
  }
}

- (void)handleTitle:(NSString*)title {
  _window.title = [self windowTitle:title];
}

#pragma mark - NSWindowDelegate

- (BOOL)windowShouldClose:(NSWindow*)sender {
  // Closing the window cancels the link.
  [self cancel];
  return NO;
}

@end

@implementation BRLinkResolver {
  __weak id<BRBrowserCounter> _counter;
  NSMutableArray<BRLinkResolution*>* _resolutions;
}

- (instancetype)initWithBrowserCounter:(id<BRBrowserCounter>)counter {
  if ((self = [super init])) {
    _counter = counter;
    _resolutions = [NSMutableArray array];
  }
  return self;
}

- (void)resolveURL:(NSString*)url
           wrapper:(NSString*)wrapperName
      userWrappers:(std::vector<brouk::LinkWrapperRule>)userWrappers
          onScreen:(nullable NSScreen*)screen
              done:(void (^)(NSString* _Nullable finalURL))done {
  __weak BRLinkResolver* weakSelf = self;
  BRLinkResolution* resolution = [[BRLinkResolution alloc]
       initWithURL:url
           wrapper:wrapperName
      userWrappers:std::move(userWrappers)
            screen:screen
           counter:_counter
              done:done
          finished:^(BRLinkResolution* finished) {
            BRLinkResolver* strongSelf = weakSelf;
            if (strongSelf) {
              [strongSelf->_resolutions removeObject:finished];
            }
          }];
  [_resolutions addObject:resolution];
}

- (void)cancelAll {
  for (BRLinkResolution* resolution in [_resolutions copy]) {
    [resolution cancel];
  }
}

+ (void)removeLeftoverDataIn:(NSString*)webDataRoot {
  NSFileManager* files = NSFileManager.defaultManager;
  for (NSString* name in [files contentsOfDirectoryAtPath:webDataRoot
                                                    error:nil]) {
    if ([name hasPrefix:@"link-check-"]) {
      [files removeItemAtPath:[webDataRoot stringByAppendingPathComponent:name]
                        error:nil];
    }
  }
}

@end
