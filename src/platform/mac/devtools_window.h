// Chromium DevTools for one of Brouk's own pages (settings, profile manager,
// the dock's tools page), in a window of its own (006: "internal HTML pages
// can be inspected"). Only DevTools: our own tools work on web tabs and
// their profiles, which internal pages do not have. Main thread only.

#import <Cocoa/Cocoa.h>

#include "cef/devtools_preferences.h"
#include "include/cef_browser.h"
#include "include/cef_request_context.h"
#import "platform/mac/devtools_dock.h"

NS_ASSUME_NONNULL_BEGIN

@class BRDevToolsWindowController;

@protocol BRDevToolsWindowOwner <NSObject>
// DevTools has closed (the user closed the window, or the page went away).
- (void)devToolsWindowDidClose:(BRDevToolsWindowController*)controller;
@end

@interface BRDevToolsWindowController : NSWindowController

// Opens DevTools for `inspected`; if `point` is not NSNotFound, the element
// there is selected in Elements. `context`, `preferences` and `counter` are
// the same as for a tab's dock.
- (instancetype)initWithInspectedBrowser:(CefRefPtr<CefBrowser>)inspected
                                   title:(NSString*)title
                                 inspect:(NSPoint)point
                                 context:(CefRefPtr<CefRequestContext>)context
                             preferences:
                                 (brouk::DevToolsPreferences*)preferences
                                 counter:(id<BRBrowserCounter>)counter
                                   owner:(id<BRDevToolsWindowOwner>)owner;
- (instancetype)initWithWindow:(nullable NSWindow*)window NS_UNAVAILABLE;
- (nullable instancetype)initWithCoder:(NSCoder*)coder NS_UNAVAILABLE;

- (void)inspectAt:(NSPoint)point;
- (void)showPanel:(NSString*)panel;
- (void)enterInspectElementMode;
// Closes DevTools and the window; the owner hears devToolsWindowDidClose:.
- (void)closeDevTools;
// The inspected page is going away: closes without telling the owner.
- (void)detach;

@end

NS_ASSUME_NONNULL_END
