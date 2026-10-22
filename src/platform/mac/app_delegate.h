// Application delegate: main menu, browser windows, and the quit sequence
// (CEF may only shut down after every browser has closed). Main thread only.

#import <Cocoa/Cocoa.h>

#include "core/launch_args.h"
#include "include/cef_application_mac.h"

NS_ASSUME_NONNULL_BEGIN

// NSApplication subclass required by CEF (CefAppProtocol).
@interface BRApplication : NSApplication <CefAppProtocol>
@end

@interface BRAppDelegate : NSObject <NSApplicationDelegate>

// Called once CEF is initialized (CefBrowserProcessHandler).
- (void)cefContextInitialized;
// The app was launched again while running, with these arguments.
- (void)relaunchedWithRequest:(const brouk::LaunchRequest&)request;
// Cmd+Q / Quit menu: closes all windows, then quits CEF.
- (void)tryToTerminateApplication;
// After the message loop ended, before CefShutdown: drops every window, tab
// and CEF object we still hold.
- (void)releaseBrowserObjects;

@end

NS_ASSUME_NONNULL_END
