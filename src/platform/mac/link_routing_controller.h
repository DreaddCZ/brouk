// Link routing (010) on macOS: takes a link from another app or a new tab
// from inside the browser, looks through link wrappers, asks
// brouk::DecideProfile and brouk::DecideWindow where it belongs, shows the
// picker when the user has to choose, and opens it there. Owns the rules
// file (link-routing.yaml). Main thread only.

#import <Cocoa/Cocoa.h>

#include <optional>
#include <vector>

#include "core/link_router.h"
#include "core/link_routing_rules.h"
#include "core/profile_registry.h"
#include "core/result.h"
#include "include/cef_request_context.h"
#import "platform/mac/devtools_dock.h"

@class BRBrowserWindowController;
@class BRTab;

NS_ASSUME_NONNULL_BEGIN

// Implemented by the app delegate; counts the neutral browsers that
// resolve opaque links, so quitting waits for them.
@protocol BRLinkRoutingHost <BRBrowserCounter>
- (brouk::ProfileId)defaultProfileId;
- (std::vector<brouk::ProfileInfo>)profileList;
- (brouk::ProfileInfo)profileInfo:(const brouk::ProfileId&)profile;
// All browser windows, front window first.
- (NSArray<BRBrowserWindowController*>*)browserWindowControllers;
// A new, empty browser window.
- (BRBrowserWindowController*)openWindow;
// "When the chosen window is on another Space": move it here.
- (BOOL)moveChosenWindowToActiveSpace;
@end

@interface BRLinkRoutingController : NSObject

- (instancetype)initWithHost:(id<BRLinkRoutingHost>)host
                  configFile:(NSString*)path;

// Reads the rules file again (it changed, or at start); problems go to the
// log until the settings page shows them.
- (void)reload;
@property(nonatomic, readonly) const brouk::LinkRoutingConfig& config;
@property(nonatomic, readonly) const std::vector<std::string>& problems;
// Saves new rules (settings page).
- (std::optional<brouk::Error>)setConfig:(brouk::LinkRoutingConfig)config;

// A link from another app; `appName` is shown in the picker. Links that
// arrive while the picker is open wait for it.
- (void)openExternalURL:(NSString*)url fromApp:(NSString*)appName;

// A new tab or window from inside the browser (`source` kNewTab or
// kTypedNewTab; `tab` is the opener, or the new tab the address was typed
// into). Returns NO when the link stays in `tab`'s profile and the caller
// opens it as before; YES when routing took it over.
- (BOOL)routeURL:(NSString*)url
          source:(brouk::LinkSource)source
         fromTab:(BRTab*)tab
        inWindow:(BRBrowserWindowController*)controller
      foreground:(BOOL)foreground;

// A page whose profile is already known, opened from an internal page in
// a tab of another profile (a linked bookmark on the bookmark manager, an
// entry on the history page; 011, 013). No rules or wrappers: only the
// window choice, with the picker's window step when the profile's group is
// in several windows. `sourceName` is shown in the picker ("From History");
// `bookmark` counts the visit as opened from a bookmark.
- (void)openURL:(NSString*)url
      inProfile:(const brouk::ProfileId&)profile
     sourceName:(NSString*)sourceName
       inWindow:(BRBrowserWindowController*)controller
     foreground:(BOOL)foreground
       bookmark:(BOOL)bookmark;

// Quitting: closes the picker and stops resolving links.
- (void)cancelPending;

@end

NS_ASSUME_NONNULL_END
