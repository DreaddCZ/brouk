// The app's bookmarks (011): the core's BookmarkModel, loaded from
// bookmarks.json at launch and saved shortly after each change. Every view
// (menu, star, bar, manager page) changes bookmarks through this object and
// redraws on BRBookmarksDidChangeNotification. Main thread only.

#import <Cocoa/Cocoa.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/bookmark_model.h"
#include "core/result.h"

NS_ASSUME_NONNULL_BEGIN

extern NSNotificationName const BRBookmarksDidChangeNotification;

// Where a bookmark opens (011): click, ⌘-click or middle-click, ⇧⌘-click,
// "Open in New Window".
typedef NS_ENUM(NSInteger, BRBookmarkOpenDisposition) {
  BRBookmarkOpenCurrentTab,
  BRBookmarkOpenBackgroundTab,
  BRBookmarkOpenForegroundTab,
  BRBookmarkOpenNewWindow,
};

@interface BRBookmarkStore : NSObject

- (instancetype)initWithFile:(NSString*)path NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

- (const brouk::BookmarkModel&)model;

// Runs `change` on the model; when it changed anything, observers are told
// and the file is saved soon.
- (void)change:(const std::function<void(brouk::BookmarkModel&)>&)change;

// Removes items (011: any deletion can be undone); the last removal can be
// put back with -undoRemove.
- (void)removeItems:(const std::vector<brouk::BookmarkId>&)ids;
- (BOOL)canUndoRemove;
- (void)undoRemove;

// The folder the add dialog offers first (Chrome: the last one used); the
// bar until something else was chosen.
@property(nonatomic) brouk::BookmarkId lastUsedFolder;

// Writes pending changes now (at quit).
- (void)saveNow;

@end

NS_ASSUME_NONNULL_END
