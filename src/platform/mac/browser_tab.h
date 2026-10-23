// One tab: its CEF browser, the view hosting it and the state the UI shows.
// Main thread only.

#import <Cocoa/Cocoa.h>

#include <optional>
#include <string>

#import "platform/mac/devtools_dock.h"

#include "cef/devtools_preferences.h"
#include "cef/tab_client.h"
#include "core/history_store.h"
#include "core/profile_id.h"
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
// History (013): a page was shown, or its title changed.
- (void)tab:(BRTab*)tab
    visitedURL:(const std::string&)url
         title:(const std::string&)title
    transition:(brouk::VisitTransition)transition;
- (void)tab:(BRTab*)tab titleChanged:(NSString*)title forURL:(NSString*)url;
// The page last recorded forwarded to the next one by itself.
- (void)tab:(BRTab*)tab visitRedirectedFrom:(const std::string&)url;
- (void)tab:(BRTab*)tab
        openURL:(NSString*)url
    disposition:(brouk::OpenDisposition)disposition;
// Settings the tab follows (004).
- (brouk::TabClientDelegate::SearchEngine)searchEngine;
- (BOOL)askWhereToSaveDownloads;
// Developer tools (006): the tab's state changed (the toolbar toggle shows
// it); the dock's position buttons and divider change the shared position
// and size; keys DevTools did not use.
- (void)tabDevToolsChanged:(BRTab*)tab;
- (void)tab:(BRTab*)tab wantsDockPosition:(NSString*)position;
- (void)tab:(BRTab*)tab resizedDockTo:(CGFloat)size;
- (NSString*)dockPosition;
- (CGFloat)dockSize;
// Where the DevTools frontend runs, its settings, and the app's browser
// counter (006).
- (CefRefPtr<CefRequestContext>)devToolsContext;
- (brouk::DevToolsPreferences*)devToolsPreferences;
- (id<BRBrowserCounter>)browserCounter;
- (NSString*)secretHeaderNames;
// Request rules of a profile (006).
- (brouk::ToolsHost::Rules)requestRulesForProfile:
    (const brouk::ProfileId&)profile;
- (std::optional<brouk::Error>)setRequestRules:(brouk::RequestRuleSet)rules
                                    forProfile:(const brouk::ProfileId&)profile;
@end

@interface BRTab : NSObject

@property(nonatomic, readonly) brouk::TabId tabId;
@property(nonatomic, weak) id<BRTabDelegate> delegate;
// The tab as placed in the window: the page and, while dev tools are on
// and docked, the dock (006). Sized to the window's content area.
@property(nonatomic, readonly) NSView* tabView;
// The page: hosts the CEF browser view and the tab's overlays (dialogs,
// prompts). Inside `tabView`.
@property(nonatomic, readonly) NSView* containerView;

// Developer tools (006), on or off per tab. Turning them on opens the
// dock; turning them off (or closing the dock) closes it.
@property(nonatomic, readonly) BOOL devToolsOn;
- (void)setDevToolsOn:(BOOL)on;
// "Inspect": turns dev tools on and selects the element at `point` (page
// coordinates, as CEF reports them).
- (void)inspectElementAt:(NSPoint)point;
// Turns dev tools on and shows the console / starts picking an element.
- (void)showDevToolsConsole;
- (void)startDevToolsElementPicker;
// A request rule changed something on the current page (006); shown on
// the dev tools toggle. Reset when a new page starts loading.
@property(nonatomic, readonly) BOOL rulesAffectedPage;
// The profile's rules changed: apply them to this tab's dock.
- (void)requestRulesChanged:(const brouk::RequestRuleSet&)rules;
// Sends changed settings to the page's renderer (JSON viewer, 006).
- (void)sendRendererConfigWithJsonViewer:(BOOL)jsonViewer;
// The shared dock position ("bottom", "right", "left", "window") and size.
- (void)setDockPosition:(NSString*)position size:(CGFloat)size;

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
// The next page shown was typed in the address bar (history, 013).
@property(nonatomic) BOOL nextVisitTyped;

// `profileId`: the profile the tab belongs to (005); its browser must be
// created in that profile's request context. It never changes: moving a tab
// to another profile opens a new tab there.
- (instancetype)initWithId:(brouk::TabId)tabId
                   profile:(const brouk::ProfileId&)profileId
                  delegate:(id<BRTabDelegate>)delegate;

@property(nonatomic, readonly) const brouk::ProfileId& profileId;

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
