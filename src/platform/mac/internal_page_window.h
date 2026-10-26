// A window showing one internal page (brouk://..., specs/001): the profile
// manager (005), settings (004). The page's bridge messages go to `handler`.
// Main thread only.

#import <Cocoa/Cocoa.h>

#include <memory>

#include "cef/devtools_preferences.h"
#include "include/cef_request_context.h"
#include "include/wrapper/cef_message_router.h"
#import "platform/mac/devtools_dock.h"

NS_ASSUME_NONNULL_BEGIN

@class BRInternalPageWindowController;

// Also counts DevTools browsers opened for the page (006).
@protocol BRInternalPageOwner <BRBrowserCounter>
- (void)internalPageDidCreateBrowser;
// The browser has closed; the owner drops the controller.
- (void)internalPageDidClose:(BRInternalPageWindowController*)controller;
// Keys the page did not use go to the menu bar.
- (BOOL)internalPageHandleKeyEvent:(NSEvent*)event;
// For inspecting the page with DevTools, as for tabs (006).
- (CefRefPtr<CefRequestContext>)devToolsContext;
- (brouk::DevToolsPreferences*)devToolsPreferences;
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

// The same; the window keeps its size and position under `frameName`
// across closing and restarts (help, 016).
- (instancetype)
    initWithOwner:(id<BRInternalPageOwner>)owner
              url:(NSString*)url
            title:(NSString*)title
             size:(NSSize)size
        frameName:(nullable NSString*)frameName
          handler:(std::unique_ptr<CefMessageRouterBrowserSide::Handler>)handler
          context:(CefRefPtr<CefRequestContext>)context;

// Closes the page (and then the window).
- (void)closePage;
// Runs a script in the page (e.g. help showing a section again).
- (void)executeJavaScript:(NSString*)code;
// Navigates the page; only a changed #fragment keeps the page loaded.
- (void)loadURL:(NSString*)url;

@end

NS_ASSUME_NONNULL_END
