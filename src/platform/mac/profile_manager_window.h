// The profile manager window (005): shows brouk://profiles in its own
// window, like Chrome's profile picker. Main thread only.

#import <Cocoa/Cocoa.h>

#include "cef/profile_manager_handler.h"
#include "include/cef_request_context.h"

NS_ASSUME_NONNULL_BEGIN

@class BRProfileManagerWindowController;

@protocol BRProfileManagerOwner <NSObject>
- (void)profileManagerDidCreateBrowser;
// The browser has closed; the owner drops the controller.
- (void)profileManagerDidClose:(BRProfileManagerWindowController*)controller;
// Keys the page did not use go to the menu bar.
- (BOOL)profileManagerHandleKeyEvent:(NSEvent*)event;
@end

@interface BRProfileManagerWindowController : NSWindowController

// `delegate` answers the page's requests and must outlive this window;
// `context` is the internal pages' request context.
- (instancetype)initWithOwner:(id<BRProfileManagerOwner>)owner
                     delegate:(brouk::ProfileManagerDelegate*)delegate
                      context:(CefRefPtr<CefRequestContext>)context;

// Closes the page (and then the window).
- (void)closeManager;

@end

NS_ASSUME_NONNULL_END
