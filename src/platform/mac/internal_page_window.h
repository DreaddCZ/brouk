// A window showing one internal page (brouk://..., specs/001): the profile
// manager (005), settings (004). The page's bridge messages go to `handler`.
// Main thread only.

#import <Cocoa/Cocoa.h>

#include <memory>

#include "include/cef_request_context.h"
#include "include/wrapper/cef_message_router.h"

NS_ASSUME_NONNULL_BEGIN

@class BRInternalPageWindowController;

@protocol BRInternalPageOwner <NSObject>
- (void)internalPageDidCreateBrowser;
// The browser has closed; the owner drops the controller.
- (void)internalPageDidClose:(BRInternalPageWindowController*)controller;
// Keys the page did not use go to the menu bar.
- (BOOL)internalPageHandleKeyEvent:(NSEvent*)event;
@end

@interface BRInternalPageWindowController : NSWindowController

// `context` is the internal pages' request context.
- (instancetype)
    initWithOwner:(id<BRInternalPageOwner>)owner
              url:(NSString*)url
            title:(NSString*)title
             size:(NSSize)size
          handler:(std::unique_ptr<CefMessageRouterBrowserSide::Handler>)handler
          context:(CefRefPtr<CefRequestContext>)context;

// Closes the page (and then the window).
- (void)closePage;

@end

NS_ASSUME_NONNULL_END
