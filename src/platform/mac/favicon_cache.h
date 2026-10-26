// Site icons for native views (013): the bookmarks bar, the Bookmarks menu.
// Icons come from each profile's history; this keeps the ones views asked
// for, so drawing never waits for the disk. Main thread only.

#import <Cocoa/Cocoa.h>

#include <string>

#include "core/profile_id.h"

namespace brouk {
class HistoryService;
}

NS_ASSUME_NONNULL_BEGIN

// Posted when icons asked for have arrived or changed: redraw.
extern NSNotificationName const BRFaviconsDidChangeNotification;

@interface BRFaviconCache : NSObject

// `history` must outlive this object, or be detached first.
- (instancetype)initWithHistory:(brouk::HistoryService*)history
    NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

// The icon of `url` in `profile`'s history at 16 pt, else nil (not known
// yet, or none). Unknown icons are fetched, and the notification says when
// they arrive.
- (nullable NSImage*)iconForURL:(const std::string&)url
                        profile:(const brouk::ProfileId&)profile;

// A page of `profile` got an icon: icons of its address and its site that
// views asked for are fetched again.
- (void)iconChangedForURL:(const std::string&)url
                  profile:(const brouk::ProfileId&)profile;

// The history is going away (quit).
- (void)detach;

@end

NS_ASSUME_NONNULL_END
