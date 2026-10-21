// One tab: its CEF browser, the view hosting it and the state the UI shows.
// Main thread only.

#import <Cocoa/Cocoa.h>

#include <optional>
#include <string>

#include "cef/tab_client.h"
#include "core/tab_strip_model.h"
#include "include/cef_browser.h"
#include "include/cef_request_context.h"

NS_ASSUME_NONNULL_BEGIN

@class BRTab;

// Receives a tab's events; implemented by the window controller.
@protocol BRTabDelegate <NSObject>
- (void)tabDidCreateBrowser:(BRTab*)tab;
- (void)tabDidChangeState:(BRTab*)tab;
- (std::optional<brouk::PopupHost>)tab:(BRTab*)tab
                            wantsPopup:(const brouk::PopupRequest&)request;
- (void)tab:(BRTab*)tab popupAborted:(int)popupId;
- (void)tabCloseReady:(BRTab*)tab;
- (void)tabDidClose:(BRTab*)tab;
- (BOOL)tab:(BRTab*)tab handleUnhandledKeyEvent:(NSEvent*)event;
- (std::string)tab:(BRTab*)tab
    reserveDownloadPath:(const std::string&)suggested;
- (void)tab:(BRTab*)tab releaseDownloadPath:(const std::string&)path;
- (void)tab:(BRTab*)tab downloadUpdated:(const brouk::DownloadInfo&)info;
- (void)tab:(BRTab*)tab fullscreenChanged:(BOOL)fullscreen;
- (void)tab:(BRTab*)tab
        openURL:(NSString*)url
    disposition:(brouk::OpenDisposition)disposition;
@end

@interface BRTab : NSObject

@property(nonatomic, readonly) brouk::TabId tabId;
@property(nonatomic, weak) id<BRTabDelegate> delegate;
// Hosts the CEF browser view; sized to the window's content area.
@property(nonatomic, readonly) NSView* containerView;

@property(nonatomic, readonly, copy) NSString* title;
@property(nonatomic, readonly, copy) NSString* url;
@property(nonatomic, readonly, nullable) NSImage* favicon;
@property(nonatomic, readonly) BOOL isLoading;
@property(nonatomic, readonly) BOOL canGoBack;
@property(nonatomic, readonly) BOOL canGoForward;
// Close was requested (unload handlers may still be running).
@property(nonatomic, readonly) BOOL isClosing;
// A JavaScript dialog is waiting for the user in this tab.
@property(nonatomic, readonly) BOOL hasDialog;

// The page plays sound (estimated by the page script) / the tab is muted.
@property(nonatomic, readonly) BOOL isAudible;
@property(nonatomic, readonly) BOOL isMuted;
- (void)toggleMute;

// Popups blocked on the current page (newest last).
@property(nonatomic, readonly) NSArray<NSString*>* blockedPopupURLs;
- (void)clearBlockedPopups;

// Find in page state, kept per tab as in Chrome.
@property(nonatomic) BOOL findBarVisible;
@property(nonatomic, copy) NSString* findText;
@property(nonatomic, readonly) int findCount;
@property(nonatomic, readonly) int findActiveMatch;
// What the user typed in the address bar but has not submitted yet; kept per
// tab like in Chrome.
@property(nonatomic, copy, nullable) NSString* pendingAddressText;

- (instancetype)initWithId:(brouk::TabId)tabId
                  delegate:(id<BRTabDelegate>)delegate;

// Creates the browser in the container view and loads `url`.
- (void)createBrowserWithURL:(NSString*)url
              requestContext:(CefRefPtr<CefRequestContext>)requestContext;

// The client to hand to CEF for a popup that this tab will host.
- (CefRefPtr<CefClient>)client;

- (CefRefPtr<CefBrowser>)browser;
- (void)loadURL:(NSString*)url;
- (void)goBack;
- (void)goForward;
- (void)reload;
- (void)reloadIgnoringCache;
- (void)stopLoading;
- (void)focusContent;

// Find in page; an empty text stops finding.
- (void)findText:(NSString*)text forward:(BOOL)forward findNext:(BOOL)findNext;
- (void)stopFinding;

// Zoom with Chrome's preset steps, remembered per site in the profile.
- (void)zoom:(cef_zoom_command_t)command;
// Chromium zoom level of the page (0 = 100 %).
- (double)zoomLevel;

- (void)print;
// Leaves HTML full screen (if the page is in it).
- (void)exitFullscreen;

// Starts closing: runs the page's beforeunload handler, which may cancel.
- (void)requestClose;
// Closes without asking the page (e.g. app shutdown after confirmation).
- (void)forceClose;

@end

NS_ASSUME_NONNULL_END
