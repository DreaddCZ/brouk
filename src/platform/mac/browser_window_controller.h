// A browser window: tab strip, toolbar with address bar, and the active tab's
// web content. The tab order and selection live in brouk::TabStripModel (core);
// this class renders it and forwards user actions. Main thread only.

#import <Cocoa/Cocoa.h>

#include <string>

#include "cef/tab_client.h"
#include "core/profile_registry.h"
#include "include/cef_request_context.h"

NS_ASSUME_NONNULL_BEGIN

@class BRBrowserWindowController;

// Implemented by the app delegate: app-wide bookkeeping of windows and
// browsers (needed to quit CEF only after every browser has closed).
@protocol BRBrowserWindowOwner <NSObject>
// Profiles (005): each has its own request context (isolated web data).
- (CefRefPtr<CefRequestContext>)requestContextForProfile:
    (const brouk::ProfileId&)profile;
- (brouk::ProfileId)defaultProfileId;
// Name and colour; a placeholder for a profile that no longer exists.
- (brouk::ProfileInfo)profileInfo:(const brouk::ProfileId&)profile;
- (void)browserWindowDidCreateBrowser;
- (void)browserWindowDidCloseBrowser;
- (void)browserWindowWillClose:(BRBrowserWindowController*)controller;
// "Open Link in New Window": a new window with a tab in `profile`.
- (void)openWindowWithURL:(NSString*)url
                  profile:(const brouk::ProfileId&)profile;
// A popup window for window.open() with features; `size` is the content size
// the page asked for.
- (BRBrowserWindowController*)openPopupWindowWithContentSize:(NSSize)size;
- (void)openNewWindow;
// Settings (004) tabs and the address bar follow.
- (brouk::TabClientDelegate::SearchEngine)searchEngine;
- (BOOL)askWhereToSaveDownloads;
// Downloads (app-wide, so concurrent downloads never share a file name).
- (std::string)reserveDownloadPath:(const std::string&)suggested;
- (void)releaseDownloadPath:(const std::string&)path;
@end

@interface BRBrowserWindowController : NSWindowController <NSWindowDelegate>

- (instancetype)initWithOwner:(id<BRBrowserWindowOwner>)owner;
// A popup window (no tab strip, read-only address) of the given content size.
- (instancetype)initWithOwner:(id<BRBrowserWindowOwner>)owner
             popupContentSize:(NSSize)popupSize;

// Opens a tab at the end of `profile`'s group (the default profile's for the
// first form). `url` nil opens an empty new tab with the address bar focused.
- (void)openTabWithURL:(nullable NSString*)url activate:(BOOL)activate;
- (void)openTabWithURL:(nullable NSString*)url
               profile:(const brouk::ProfileId&)profile
              activate:(BOOL)activate;

// Profile names, colours or the default profile changed: redraw.
- (void)profilesDidChange;

// Problems found in the settings files; a toolbar warning leads to them.
@property(nonatomic) NSUInteger settingsProblemCount;

// Tabs of a profile in this window (including tabs still closing), and
// closing them (a profile is deleted, 005).
- (NSUInteger)tabCountInProfile:(const brouk::ProfileId&)profile;
- (void)closeTabsInProfile:(const brouk::ProfileId&)profile;

// Starts closing every tab (pages may ask to stay via beforeunload); the
// window closes when the last tab is gone.
- (void)closeAllTabs;

// Number of tabs, including tabs that are still closing.
- (NSUInteger)liveTabCount;

// A popup window (window.open with features): has no tab strip.
@property(nonatomic, readonly) BOOL isPopup;

@end

NS_ASSUME_NONNULL_END
