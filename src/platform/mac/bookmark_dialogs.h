// The native bookmark dialogs (011): the bubble under the address bar's
// star (⌘D), and the edit dialog used by the bar, the menu and the
// manager page. Main thread only.

#import <Cocoa/Cocoa.h>

#include <string>
#include <vector>

#import "platform/mac/bookmark_store.h"

#include "core/bookmark_model.h"
#include "core/profile_registry.h"

NS_ASSUME_NONNULL_BEGIN

@interface BRBookmarkBubble : NSObject

// Shows the bubble for bookmark `bookmarkId` below `anchor`. The bookmark
// already exists (Chrome saves it when the bubble opens); name, folder and
// profile changes are applied when the bubble closes, Remove deletes it.
// Folders offered are those visible in `activeProfile`.
+ (void)showForBookmark:(brouk::BookmarkId)bookmarkId
                  store:(BRBookmarkStore*)store
               profiles:(const std::vector<brouk::ProfileInfo>&)profiles
          activeProfile:(const brouk::ProfileId&)activeProfile
             newlyAdded:(BOOL)newlyAdded
             relativeTo:(NSView*)anchor
                removed:(void (^)(void))removed;

@end

// "Bookmark removed — Undo" for a few seconds after a deletion (011: any
// deletion can be undone).
@interface BRBookmarkUndoNotice : NSObject
+ (void)showMessage:(NSString*)message
              store:(BRBookmarkStore*)store
         relativeTo:(NSView*)anchor;
@end

// What the edit dialog edits.
struct BookmarkEditorValues {
  std::string name;
  std::string url;               // Bookmarks only.
  brouk::BookmarkId folder = 0;  // The folder the item is (or goes) in.
  brouk::ProfileId profile;      // "" for general.
};

enum class BookmarkEditorKind {
  kBookmark,             // Name, URL, folder and profile.
  kFolder,               // Name and profile.
  kNewFolderWithParent,  // Name, folder (its parent) and profile.
};

@interface BRBookmarkEditor : NSObject

// A sheet on `window`. `editing` is the item being edited (0 when adding):
// it and its contents are left out of the folder list. `done` gets the
// values on Save; nothing happens on Cancel.
+ (void)runKind:(BookmarkEditorKind)kind
            title:(NSString*)title
           values:(const BookmarkEditorValues&)values
          editing:(brouk::BookmarkId)editing
            store:(BRBookmarkStore*)store
         profiles:(const std::vector<brouk::ProfileInfo>&)profiles
    activeProfile:(const brouk::ProfileId&)activeProfile
           window:(NSWindow*)window
             done:(void (^)(const BookmarkEditorValues& values))done;

@end

NS_ASSUME_NONNULL_END
