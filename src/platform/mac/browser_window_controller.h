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
// Downloads (app-wide, so concurrent downloads never share a file name).
- (std::string)reserveDownloadPath:(const std::string&)suggested;
- (void)releaseDownloadPath:(const std::string&)path;
@end

@interface BRBrowserWindowController : NSWindowController <NSWindowDelegate>

- (instancetype)initWithOwner:(id<BRBrowserWindowOwner>)owner;

// Opens a tab at the end of the strip. `url` nil opens an empty new tab with
// the address bar focused.
- (void)openTabWithURL:(nullable NSString*)url activate:(BOOL)activate;

// Starts closing every tab (pages may ask to stay via beforeunload); the
// window closes when the last tab is gone.
- (void)closeAllTabs;

// Number of tabs, including tabs that are still closing.
- (NSUInteger)liveTabCount;

@end

NS_ASSUME_NONNULL_END
