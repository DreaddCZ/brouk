// Entry point of the browser (main) process on macOS.

#import <Cocoa/Cocoa.h>

#import "platform/mac/app_delegate.h"
#import "platform/mac/data_paths.h"

#include <cstdio>
#include <cstring>

#include "brouk/app_info.h"
#include "brouk/build_info.h"
#include "cef/browser_app.h"
#include "include/cef_app.h"
#include "include/wrapper/cef_library_loader.h"

int main(int argc, char* argv[]) {
  // "Brouk 0.4.0 (a1b2c3d)": every report points to an exact build (019).
  if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
    std::printf("%s %s (%s)\n", brouk::kAppName, brouk::kAppVersion,
                brouk::kAppCommit);
    return 0;
  }
  // The CEF framework is loaded at runtime, as the macOS sandbox requires.
  CefScopedLibraryLoader library_loader;
  if (!library_loader.LoadInMain()) {
    return 1;
  }

  CefMainArgs main_args(argc, argv);

  @autoreleasepool {
    // Must run before anything touches NSApp, so NSApp is a BRApplication.
    [BRApplication sharedApplication];
    if (![NSApp isKindOfClass:[BRApplication class]]) {
      return 1;
    }

    // Session restore will be our own (003): macOS must not restore windows
    // or offer to "reopen windows" after a crash.
    [NSUserDefaults.standardUserDefaults registerDefaults:@{
      @"ApplePersistenceIgnoreState" : @YES
    }];

    BRAppDelegate* delegate = [[BRAppDelegate alloc] init];
    NSApp.delegate = delegate;

    CefSettings settings;
#if !defined(CEF_USE_SANDBOX)
    settings.no_sandbox = true;
#endif
    // Chromium's data lives in its own folder (see data_paths.h).
    CefString(&settings.root_cache_path) =
        brouk::mac::WebDataRootDirectory().UTF8String;
    NSString* helper =
        [NSString stringWithFormat:@"%@/%s Helper.app/Contents/MacOS/%s Helper",
                                   NSBundle.mainBundle.privateFrameworksPath,
                                   brouk::kAppName, brouk::kAppName];
    CefString(&settings.browser_subprocess_path) = helper.UTF8String;
#if defined(NDEBUG)
    settings.log_severity = LOGSEVERITY_WARNING;
#endif
    // Remote debugging (006): off unless enabled in the settings (or by the
    // --remote-debugging-port switch). Chromium listens on localhost only.
    settings.remote_debugging_port = [delegate remoteDebuggingPortSetting];

    __weak BRAppDelegate* weak_delegate = delegate;
    brouk::BrowserApp::Callbacks callbacks;
    callbacks.on_context_initialized = [weak_delegate] {
      [weak_delegate cefContextInitialized];
    };
    callbacks.json_viewer_enabled = [weak_delegate] {
      BRAppDelegate* app = weak_delegate;
      return !app || [app jsonViewerEnabled];
    };
    callbacks.on_relaunch = [weak_delegate](brouk::LaunchRequest request) {
      [weak_delegate relaunchedWithRequest:request];
    };
    CefRefPtr<brouk::BrowserApp> app(new brouk::BrowserApp(callbacks));

    // Returns false also when another instance is already running; that
    // instance gets OnAlreadyRunningAppRelaunch and opens the URLs.
    if (!CefInitialize(main_args, settings, app, nullptr)) {
      return CefGetExitCode();
    }

    // Runs until CefQuitMessageLoop() (after every browser has closed). The
    // inner pool makes sure windows, views and CEF objects created while
    // running are freed before CefShutdown, not after it.
    @autoreleasepool {
      CefRunMessageLoop();
      [delegate releaseBrowserObjects];
    }
    CefShutdown();
  }
  return 0;
}
