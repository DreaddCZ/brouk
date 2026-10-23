// A tab's developer tools dock (006): a header with the dock's tools (for
// now Chromium's DevTools) and buttons to move or close it, and the DevTools
// browser below. The tab places the view (bottom/right/left of the page, or
// in its own window). Main thread only.

#import <Cocoa/Cocoa.h>

#include "cef/devtools_preferences.h"
#include "include/cef_browser.h"
#include "include/cef_request_context.h"

NS_ASSUME_NONNULL_BEGIN

@class BRDevToolsDock;

// Counts browsers, so the app quits CEF only after all have closed.
@protocol BRBrowserCounter <NSObject>
- (void)browserWindowDidCreateBrowser;
- (void)browserWindowDidCloseBrowser;
@end

@protocol BRDevToolsDockDelegate <NSObject>
// The DevTools browser closed (the user closed the dock, or DevTools
// itself went away): the tab's dev tools are off.
- (void)devToolsDockDidClose:(BRDevToolsDock*)dock;
// A position button in the header: "bottom", "right", "left" or "window".
- (void)devToolsDock:(BRDevToolsDock*)dock wantsPosition:(NSString*)position;
- (BOOL)devToolsDockHandleKeyEvent:(NSEvent*)event;
// "Open in new tab" and copying from DevTools.
- (void)devToolsDock:(BRDevToolsDock*)dock openURL:(NSString*)url;
@end

@interface BRDevToolsDock : NSObject

// Opens DevTools for `inspected`; if `point` is not NSNotFound, the element
// there is selected in Elements ("Inspect"). The frontend runs in `context`
// (the internal pages' context) and keeps its settings in `preferences`.
- (instancetype)initWithInspectedBrowser:(CefRefPtr<CefBrowser>)inspected
                                 inspect:(NSPoint)point
                                 context:(CefRefPtr<CefRequestContext>)context
                             preferences:
                                 (brouk::DevToolsPreferences*)preferences
                                 counter:(id<BRBrowserCounter>)counter
                                delegate:(id<BRDevToolsDockDelegate>)delegate;
- (instancetype)init NS_UNAVAILABLE;

// Header and DevTools; the tab sizes and places it.
@property(nonatomic, readonly) NSView* view;
// The current position, to highlight its button.
@property(nonatomic, copy) NSString* position;

// "Inspect" again while open: select the element at `point`.
- (void)inspectAt:(NSPoint)point;
// Shows a DevTools panel ("console") or starts picking an element.
- (void)showPanel:(NSString*)panel;
- (void)enterInspectElementMode;
// Closes DevTools; the delegate hears devToolsDockDidClose: when done.
- (void)close;
// The inspected tab is going away: no more delegate calls; the DevTools
// browser closes (the dock stays alive until it has).
- (void)detach;

@end

NS_ASSUME_NONNULL_END
