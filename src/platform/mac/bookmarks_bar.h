// The bookmarks bar below the toolbar (011): the "Bookmarks bar" folder's
// items visible in the active tab's profile, folders as drop-down menus,
// a "»" menu for what does not fit, drag and drop to reorder and to add
// links, and the right-click menu. Main thread only.

#import <Cocoa/Cocoa.h>

#include <cstddef>

#import "platform/mac/bookmark_store.h"

#include "core/bookmark_model.h"
#include "core/profile_id.h"

NS_ASSUME_NONNULL_BEGIN

// Pasteboard type of a bookmark dragged inside the app: its id as text.
extern NSPasteboardType const BRBookmarkPasteboardType;

@class BRBookmarksBar;

// What the bar asks of its window.
@protocol BRBookmarksBarDelegate <NSObject>
- (void)bookmarksBar:(BRBookmarksBar*)bar
        openBookmark:(brouk::BookmarkId)bookmark
         disposition:(BRBookmarkOpenDisposition)disposition;
- (void)bookmarksBar:(BRBookmarksBar*)bar openAllIn:(brouk::BookmarkId)folder;
- (void)bookmarksBar:(BRBookmarksBar*)bar edit:(brouk::BookmarkId)item;
- (void)bookmarksBar:(BRBookmarksBar*)bar remove:(brouk::BookmarkId)item;
// Add Bookmark… / Add Folder… at `index` of `folder`.
- (void)bookmarksBar:(BRBookmarksBar*)bar
       addBookmarkIn:(brouk::BookmarkId)folder
               index:(std::size_t)index;
- (void)bookmarksBar:(BRBookmarksBar*)bar
         addFolderIn:(brouk::BookmarkId)folder
               index:(std::size_t)index;
// A link (or the address bar's site icon) was dropped.
- (void)bookmarksBar:(BRBookmarksBar*)bar
             dropURL:(NSString*)url
               title:(NSString*)title
              folder:(brouk::BookmarkId)folder
               index:(std::size_t)index;
- (void)bookmarksBarToggleAlwaysShow:(BRBookmarksBar*)bar;
- (BOOL)bookmarksBarAlwaysShown:(BRBookmarksBar*)bar;
@end

@interface BRBookmarksBar : NSView <NSDraggingSource>

@property(nonatomic, weak) id<BRBookmarksBarDelegate> delegate;

// Shows `store`'s bar folder as seen in `profile`; rebuilt only when the
// bookmarks or the profile changed.
- (void)showStore:(BRBookmarkStore*)store
          profile:(const brouk::ProfileId&)profile;

@end

// A button that can be dragged out as a link (the address bar's site icon,
// 011: dropping it on the bar bookmarks the page). `link` gives the URL and
// title, or nil when there is nothing to drag.
@interface BRLinkDragButton : NSButton <NSDraggingSource>
@property(nonatomic, copy, nullable) NSArray<NSString*>* _Nullable (^link)(void)
    ;
@end

NS_ASSUME_NONNULL_END
