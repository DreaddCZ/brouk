// A browser window: tab strip, toolbar with address bar, and the active tab's
// web content. The tab order and selection live in brouk::TabStripModel (core);
// this class renders it and forwards user actions. Main thread only.

#import <Cocoa/Cocoa.h>

#include <string>

#include "include/cef_request_context.h"

NS_ASSUME_NONNULL_BEGIN

@class BRBrowserWindowController;

// Implemented by the app delegate: app-wide bookkeeping of windows and
// browsers (needed to quit CEF only after every browser has closed).
@protocol BRBrowserWindowOwner <NSObject>
- (CefRefPtr<CefRequestContext>)requestContext;
- (void)browserWindowDidCreateBrowser;
- (void)browserWindowDidCloseBrowser;
- (void)browserWindowWillClose:(BRBrowserWindowController*)controller;
// "Open Link in New Window".
- (void)openWindowWithURL:(NSString*)url;
// A popup window for window.open() with features; `size` is the content size
// the page asked for.
- (BRBrowserWindowController*)openPopupWindowWithContentSize:(NSSize)size;
- (void)openNewWindow;
// Downloads (app-wide, so concurrent downloads never share a file name).
- (std::string)reserveDownloadPath:(const std::string&)suggested;
- (void)releaseDownloadPath:(const std::string&)path;
@end

@interface BRBrowserWindowController : NSWindowController <NSWindowDelegate>

- (instancetype)initWithOwner:(id<BRBrowserWindowOwner>)owner;
// A popup window (no tab strip, read-only address) of the given content size.
- (instancetype)initWithOwner:(id<BRBrowserWindowOwner>)owner
             popupContentSize:(NSSize)popupSize;

// Opens a tab at the end of the strip. `url` nil opens an empty new tab with
// the address bar focused.
- (void)openTabWithURL:(nullable NSString*)url activate:(BOOL)activate;

// Starts closing every tab (pages may ask to stay via beforeunload); the
// window closes when the last tab is gone.
- (void)closeAllTabs;

// Number of tabs, including tabs that are still closing.
- (NSUInteger)liveTabCount;

// A popup window (window.open with features): has no tab strip.
@property(nonatomic, readonly) BOOL isPopup;

@end

NS_ASSUME_NONNULL_END
