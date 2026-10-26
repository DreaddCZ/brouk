// A tab's developer tools dock (006): a header with the dock's tools (for
// now Chromium's DevTools) and buttons to move or close it, and the DevTools
// browser below. The tab places the view (bottom/right/left of the page, or
// in its own window). Main thread only.

#import <Cocoa/Cocoa.h>

#include "cef/devtools_preferences.h"
#include "cef/devtools_tools_handler.h"
#include "core/request_rules.h"
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
// Request rules of the tab's profile (006).
- (brouk::ToolsHost::Rules)devToolsDockRequestRules:(BRDevToolsDock*)dock;
- (std::optional<brouk::Error>)devToolsDock:(BRDevToolsDock*)dock
                            setRequestRules:(brouk::RequestRuleSet)rules;
// A rule changed one of the tab's requests.
- (void)devToolsDockRulesAffectedPage:(BRDevToolsDock*)dock;
@end

@interface BRDevToolsDock : NSObject

// Opens DevTools for `inspected`; if `point` is not NSNotFound, the element
// there is selected in Elements ("Inspect"). The frontend runs in `context`
// (the internal pages' context) and keeps its settings in `preferences`.
// Without `ownTools` (for internal pages) the dock is only DevTools: no
// header, no request recording or rules.
- (instancetype)initWithInspectedBrowser:(CefRefPtr<CefBrowser>)inspected
                                 inspect:(NSPoint)point
                                 context:(CefRefPtr<CefRequestContext>)context
                             preferences:
                                 (brouk::DevToolsPreferences*)preferences
                                ownTools:(BOOL)ownTools
                                 counter:(id<BRBrowserCounter>)counter
                                delegate:(id<BRDevToolsDockDelegate>)delegate;
- (instancetype)init NS_UNAVAILABLE;

// Header and DevTools; the tab sizes and places it.
@property(nonatomic, readonly) NSView* view;
// Header names whose values are secrets in exports (a setting, 006).
@property(nonatomic, copy) NSString* secretHeaderNames;
// The current position, to highlight its button.
@property(nonatomic, copy) NSString* position;

// "Inspect" again while open: select the element at `point`.
- (void)inspectAt:(NSPoint)point;
// Shows a DevTools panel ("console") or starts picking an element.
- (void)showPanel:(NSString*)panel;
- (void)enterInspectElementMode;
// The profile's request rules, applied to the tab while the dock is open.
- (void)setRequestRules:(const brouk::RequestRuleSet&)rules;
// Closes DevTools; the delegate hears devToolsDockDidClose: when done.
- (void)close;
// The window moved to another display (see window_screen.h).
- (void)screenChanged;
// The inspected tab is going away: no more delegate calls; the DevTools
// browser closes (the dock stays alive until it has).
- (void)detach;

@end

NS_ASSUME_NONNULL_END
