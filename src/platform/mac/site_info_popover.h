// Site info popover from the address bar's lock icon (012, items 3 and 6):
// connection state, certificate, and the site's permissions, which can be
// changed or reset. Main thread only.

#import <Cocoa/Cocoa.h>

#include "include/cef_browser.h"

NS_ASSUME_NONNULL_BEGIN

@interface BRSiteInfoPopover : NSObject

// Shows the popover for `browser`'s visible page below `anchor`.
// `reload` is offered after permissions change ("Reload to apply").
+ (void)showForBrowser:(CefRefPtr<CefBrowser>)browser
            relativeTo:(NSView*)anchor
                reload:(void (^)(void))reload;

// SF Symbol and accessibility label for the address bar icon (`browser`
// may be null for a tab still loading its first page).
+ (NSString*)symbolForBrowser:(CefRefPtr<CefBrowser>)browser;
+ (NSString*)labelForBrowser:(CefRefPtr<CefBrowser>)browser;
// Red for certificate errors, otherwise nil (default tint).
+ (nullable NSColor*)tintForBrowser:(CefRefPtr<CefBrowser>)browser;

@end

NS_ASSUME_NONNULL_END
