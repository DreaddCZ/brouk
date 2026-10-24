// The ad blocker's popup from the shield button (007): the switch for the
// site, what was blocked, the third-party domains with allow/block rules,
// and links to the blocked requests list, reporting and settings. Also the
// blocked requests window. Main thread only.

#import <Cocoa/Cocoa.h>

#include <string>
#include <vector>

#include "cef/adblock_request_handler.h"

NS_ASSUME_NONNULL_BEGIN

// What the popup shows and does; implemented by the window controller.
@protocol BRAdblockPopoverActions <NSObject>
// The page's host and its requests as the ad blocker saw them.
- (NSString*)adblockPageHost;
- (const std::vector<brouk::AdblockRequestInfo>&)adblockPageRequests;
// Blocking for the whole profile, and for this site.
- (BOOL)adblockProfileEnabled;
- (BOOL)adblockSiteEnabled;
- (void)setAdblockSiteEnabled:(BOOL)enabled;
// Adds a rule to My filters and reloads the page.
- (void)addAdblockRule:(NSString*)rule;
- (NSString*)adblockListName:(const std::string&)listId;
- (void)reportBrokenSite;
- (void)openAdblockSettings;
@end

@interface BRAdblockPopover : NSObject
+ (void)showRelativeTo:(NSView*)anchor
               actions:(id<BRAdblockPopoverActions>)actions;
@end

@interface BRAdblockRequestsWindow : NSObject
// A window listing the requests that were blocked or excepted, with the
// rule and its list.
+ (void)showRequests:(const std::vector<brouk::AdblockRequestInfo>&)requests
                host:(NSString*)host
             actions:(id<BRAdblockPopoverActions>)actions;
@end

NS_ASSUME_NONNULL_END
