// Resolves opaque link wrappers (010: Mimecast, …): loads the link in a
// neutral, throw-away request context (its own folder under web/, deleted
// afterwards; no profile) without
// showing it, and reports where it leads once it leaves the wrapper hosts.
// A wrapper that stays on its own page for 5 seconds (a warning, a
// "continue" button, an error) is shown in a small "Checking link" window
// for the user. Main thread only.

#import <Cocoa/Cocoa.h>

#include <vector>

#include "core/link_routing_rules.h"

@protocol BRBrowserCounter;

NS_ASSUME_NONNULL_BEGIN

@interface BRLinkResolver : NSObject

// `counter` learns about the neutral browsers, so quitting waits for them.
- (instancetype)initWithBrowserCounter:(id<BRBrowserCounter>)counter;

// `done` gets the real URL, or nil when the user closed the "Checking
// link" window. Each call is resolved on its own.
- (void)resolveURL:(NSString*)url
           wrapper:(NSString*)wrapperName
      userWrappers:(std::vector<brouk::LinkWrapperRule>)userWrappers
          onScreen:(nullable NSScreen*)screen
              done:(void (^)(NSString* _Nullable finalURL))done;

// Quitting: stops every resolution (their `done` gets nil).
- (void)cancelAll;

// Folders of resolutions a crash left behind; before CEF starts.
+ (void)removeLeftoverDataIn:(NSString*)webDataRoot;

@end

NS_ASSUME_NONNULL_END
