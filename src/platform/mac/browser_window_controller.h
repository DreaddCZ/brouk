// A browser window: tab strip, toolbar with address bar, and the active tab's
// web content. The tab order and selection live in brouk::TabStripModel (core);
// this class renders it and forwards user actions. Main thread only.

#import <Cocoa/Cocoa.h>

#include <string>

#import "platform/mac/bookmark_store.h"
#import "platform/mac/command_palette_panel.h"
#import "platform/mac/devtools_dock.h"
#import "platform/mac/favicon_cache.h"

#include "cef/adblock_service.h"
#include "cef/devtools_preferences.h"
#include "cef/devtools_tools_handler.h"
#include "cef/history_service.h"
#include "cef/tab_client.h"
#include "core/closed_tabs.h"
#include "core/link_router.h"
#include "core/profile_registry.h"
#include "core/session.h"
#include "core/tab_strip_model.h"
#include "include/cef_request_context.h"

NS_ASSUME_NONNULL_BEGIN

@class BRBrowserWindowController;
@class BRTab;

// Implemented by the app delegate: app-wide bookkeeping of windows and
// browsers (needed to quit CEF only after every browser has closed).
@protocol BRBrowserWindowOwner <BRBrowserCounter>
// Profiles (005): each has its own request context (isolated web data).
- (CefRefPtr<CefRequestContext>)requestContextForProfile:
    (const brouk::ProfileId&)profile;
- (brouk::ProfileId)defaultProfileId;
- (BOOL)profileExists:(const brouk::ProfileId&)profile;
// Internal pages in tabs (013: brouk://history): their context and the
// bridge handler of one tab's page; null for pages that do not open in tabs.
- (CefRefPtr<CefRequestContext>)internalPagesContext;
- (std::unique_ptr<CefMessageRouterBrowserSide::Handler>)
    internalPageHandler:(NSString*)page
                  tabId:(brouk::TabId)tabId
             controller:(BRBrowserWindowController*)controller;
// brouk://settings and brouk://profiles open in their own windows.
- (IBAction)openSettings:(nullable id)sender;
// brouk://task-manager has its own window too (020).
- (IBAction)showTaskManager:(nullable id)sender;
- (IBAction)manageProfiles:(nullable id)sender;
// Name and colour; a placeholder for a profile that no longer exists.
- (brouk::ProfileInfo)profileInfo:(const brouk::ProfileId&)profile;
- (void)browserWindowDidCreateBrowser;
- (void)browserWindowDidCloseBrowser;
- (void)browserWindowWillClose:(BRBrowserWindowController*)controller;
// Link routing (010): a new tab or window from inside the browser. NO
// when the link stays in `tab`'s profile and the caller opens it as
// before; YES when routing took it over.
- (BOOL)routeLinkURL:(NSString*)url
              source:(brouk::LinkSource)source
             fromTab:(BRTab*)tab
            inWindow:(BRBrowserWindowController*)controller
          foreground:(BOOL)foreground;
// A page of a known profile opened from the bookmark manager or the
// history page in a tab of another profile: the window is chosen as for a
// routed link (010).
- (void)openURL:(NSString*)url
      inProfile:(const brouk::ProfileId&)profile
     sourceName:(NSString*)sourceName
       inWindow:(BRBrowserWindowController*)controller
     foreground:(BOOL)foreground
       bookmark:(BOOL)bookmark;
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
- (BOOL)jsonViewerEnabled;
// The developer tools dock's shared position and size (006).
- (NSString*)dockPosition;
- (CGFloat)dockSize;
- (void)setDockPosition:(NSString*)position;
- (void)setDockSize:(CGFloat)size;
- (CefRefPtr<CefRequestContext>)devToolsContext;
- (brouk::DevToolsPreferences*)devToolsPreferences;
- (NSString*)secretHeaderNames;
- (brouk::ToolsHost::Rules)requestRulesForProfile:
    (const brouk::ProfileId&)profile;
- (std::optional<brouk::Error>)setRequestRules:(brouk::RequestRuleSet)rules
                                    forProfile:(const brouk::ProfileId&)profile;
// Downloads (app-wide, so concurrent downloads never share a file name).
- (void)releaseDownloadPath:(const std::string&)path;
// The command palette (003) lists the tabs of all browser windows, front
// window first, and opens settings at one setting.
- (NSArray<BRBrowserWindowController*>*)browserWindowControllers;
// History (013, 003): null before CEF is ready.
- (nullable brouk::HistoryService*)historyService;
// A page was recorded or removed: Recently Visited may have changed.
- (void)historyChangedForProfile:(const brouk::ProfileId&)profile;
// Site icons for native views (013): null before CEF is ready.
- (nullable BRFaviconCache*)faviconCache;
// A page of `profile` got its favicon (013), already sent to the history.
- (void)faviconChangedForURL:(const std::string&)url
                     profile:(const brouk::ProfileId&)profile;
// Reopen Closed Tab (013): a tab closed on its own, or a window closed with
// its tabs (left to right).
- (void)browserWindow:(BRBrowserWindowController*)controller
            closedTab:(brouk::ClosedTab)tab;
- (void)browserWindow:(BRBrowserWindowController*)controller
    closedWindowWithTabs:(std::vector<brouk::ClosedTab>)tabs;
// Tabs, pages, order or the window frame changed: the saved session (015)
// is updated soon.
- (void)browserWindowSessionChanged;
// Address bar suggestions (003): the sources the settings allow.
- (BOOL)historySuggestionsEnabled;
// "" when search suggestions are off or the engine has none.
- (std::string)searchSuggestTemplate;
- (void)openSettingsAtKey:(NSString*)key profile:(nullable NSString*)profileId;
// Help (016): the help window at a section (nil: the top), and the sections
// for the command palette (dictionaries with "anchor" and "title").
- (void)openHelpAt:(nullable NSString*)anchor;
- (NSArray<NSDictionary<NSString*, NSString*>*>*)helpSections;
// Bookmarks (011): the app's one tree, and the profiles for its dialogs.
- (BRBookmarkStore*)bookmarkStore;
- (std::vector<brouk::ProfileInfo>)profileList;
- (BOOL)bookmarkSuggestionsEnabled;
// ⇧⌘B: the bookmarks bar on every tab, or on the new tab page only.
- (IBAction)toggleBookmarksBar:(nullable id)sender;
// Ad blocking (007): null before CEF is ready; allowing a site adds it to
// the profile's allowlist.
- (nullable brouk::AdblockService*)adblockService;
- (void)setAdblockSite:(const std::string&)host
               allowed:(BOOL)allowed
             inProfile:(const brouk::ProfileId&)profile;
// Appends a rule to My filters; `done` runs once the engines use it.
- (void)addAdblockRule:(const std::string&)rule done:(void (^)(void))done;
// Password manager (008): null before CEF is ready.
- (nullable brouk::PasswordManager*)passwordManager;
// Form autofill (009): null before CEF is ready.
- (nullable brouk::AutofillManager*)autofillManager;
// Downloads (014): null before CEF is ready; a path in the profile's
// download folder.
- (nullable brouk::DownloadService*)downloadService;
- (brouk::DownloadTarget)reserveDownloadPath:(const std::string&)suggested
                                  forProfile:(const brouk::ProfileId&)profile;
// Task manager (020): opens it with the tab's row selected.
- (void)showTaskManagerAtTab:(brouk::TabId)tabId;
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
// The same; `bookmark` counts the visit as opened from a bookmark (013).
- (void)openTabWithURL:(nullable NSString*)url
               profile:(const brouk::ProfileId&)profile
              activate:(BOOL)activate
              bookmark:(BOOL)bookmark;

// Link routing (010).
// The active tab, if any.
- (nullable BRTab*)activeTab;
// The profile groups, left to right (the default profile's always last).
- (std::vector<brouk::TabStripModel::Group>)tabGroups;
// Profiles with a group here: those with tabs, and the default profile.
- (std::vector<brouk::ProfileId>)groupProfiles;
// A routed link that stays in its source profile: after `opener` when it
// is still in this window, else at the end of `profile`'s group.
- (void)openURL:(NSString*)url
      nextToTab:(nullable BRTab*)opener
        profile:(const brouk::ProfileId&)profile
     foreground:(BOOL)foreground;
// An address typed into `tab` (a new tab), loaded there after routing.
- (void)loadTypedURL:(NSString*)url inTab:(BRTab*)tab;
// Closes `tab` if it is still an empty new tab (its address was routed to
// another profile).
- (void)closeTabIfUnused:(BRTab*)tab;

// Profile names, colours or the default profile changed: redraw.
- (void)profilesDidChange;

// Problems found in the settings files; a toolbar warning leads to them.
@property(nonatomic) NSUInteger settingsProblemCount;
// Downloads (014) changed: the toolbar button and an open popup follow.
- (void)downloadsChanged;
// Starts a failed or cancelled download again in a tab of its profile.
- (void)retryDownload:(const brouk::DownloadEntry&)entry;

// Developer tools (006): toggle side (setting), the remote debugging port
// (0: off; shows the indicator), and the shared dock position and size.
- (void)setDevToolsToggleOnLeft:(BOOL)left;
// A profile's request rules changed: apply them to its tabs.
- (void)requestRulesChanged:(const brouk::RequestRuleSet&)rules
                 forProfile:(const brouk::ProfileId&)profile;
// Settings the pages' renderers follow changed (JSON viewer).
- (void)sendRendererConfigWithJsonViewer:(BOOL)jsonViewer;
@property(nonatomic) int remoteDebuggingPort;
- (void)setDockPosition:(NSString*)position size:(CGFloat)size;

// Tabs of a profile in this window (including tabs still closing), and
// closing them (a profile is deleted, 005).
- (NSUInteger)tabCountInProfile:(const brouk::ProfileId&)profile;
// This window's tabs for the command palette (003), left to right; choosing
// one brings the window to the front and switches to the tab.
- (NSArray<BRPaletteEntry*>*)paletteTabEntries;
- (void)closeTabsInProfile:(const brouk::ProfileId&)profile;

// Starts closing every tab (pages may ask to stay via beforeunload); the
// window closes when the last tab is gone.
- (void)closeAllTabs;

// Number of tabs, including tabs that are still closing.
- (NSUInteger)liveTabCount;

// A popup window (window.open with features): has no tab strip.
@property(nonatomic, readonly) BOOL isPopup;
// Unique in this run; closed tabs remember it to come back here (013).
@property(nonatomic, readonly) std::int64_t windowId;
// The active tab's profile, or the default profile without tabs.
- (brouk::ProfileId)activeProfileId;
// Opens a closed tab again, in its profile at its old place (013).
- (void)restoreClosedTab:(const brouk::ClosedTab&)tab activate:(BOOL)activate;
// Session restore (015): the window as it is now (nullopt for popups and
// windows without tabs), and filling a new, empty window from a saved one.
- (std::optional<brouk::SessionWindow>)sessionWindow;
- (void)restoreSessionWindow:(const brouk::SessionWindow&)window;
// Task manager (020): this window's tabs left to right (not those
// closing), one of them by id, and bringing a tab to the front (switching
// to the window's Space or display).
- (NSArray<BRTab*>*)orderedTabs;
- (nullable BRTab*)tabWithId:(brouk::TabId)tabId;
- (void)goToTab:(brouk::TabId)tabId;
// "Restore pages?" after a crash (015); `restore` runs on "Restore".
- (void)showRestorePagesBar:(void (^)(void))restore;
// A web browser of one of this window's tabs in `profile`, or null.
- (CefRefPtr<CefBrowser>)anyBrowserInProfile:(const brouk::ProfileId&)profile;
// The history page with `fragment` (e.g. "clear" opens Clear browsing data):
// the history tab is reused if active, else opened as ⌘Y does.
- (void)showHistoryWithFragment:(NSString*)fragment;
// The profile of a tab (the default profile if the tab is gone).
- (brouk::ProfileId)profileOfTab:(brouk::TabId)tabId;
// History page (013): opens one of its entries in `profile`: in the history
// tab itself when it is in that profile's group (unless `newTab`), next to
// it as a new tab, or, for another profile, in the window chosen as for a
// routed link (010).
- (void)openPage:(NSString*)url
         profile:(const brouk::ProfileId&)profile
          newTab:(BOOL)newTab
     fromHistory:(brouk::TabId)historyTab;
// The same for the bookmark manager (011): `fromTab` is its tab; the visit
// counts as opened from a bookmark. An empty or deleted `profile` means the
// tab's own.
- (void)openPage:(NSString*)url
         profile:(const brouk::ProfileId&)profile
          newTab:(BOOL)newTab
         fromTab:(brouk::TabId)fromTab
        bookmark:(BOOL)bookmark;
// Opens a bookmark (011): a general one in the active tab's profile, a
// linked one always in its profile (in a new tab when the active tab is in
// another profile).
- (void)openBookmark:(brouk::BookmarkId)bookmarkId
         disposition:(BRBookmarkOpenDisposition)disposition;
// The bookmarks bar on every tab, not only on the new tab page (setting).
@property(nonatomic) BOOL bookmarksBarAlwaysShown;
// Only one tab, never used (a new tab page): replaced by a restored session.
- (BOOL)hasOnlyNewTab;

@end

NS_ASSUME_NONNULL_END
