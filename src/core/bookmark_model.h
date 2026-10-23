#ifndef BROUK_CORE_BOOKMARK_MODEL_H_
#define BROUK_CORE_BOOKMARK_MODEL_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/json.h"
#include "core/profile_id.h"
#include "core/result.h"

namespace brouk {

// Stable for the life of an item; never reused.
using BookmarkId = std::int64_t;

struct BookmarkItem {
  BookmarkId id = 0;
  BookmarkId parent = 0;  // 0 for the two top-level folders.
  bool folder = false;
  std::string name;
  std::string url;  // Bookmarks only.
  // The item's own link (empty: general). Always empty inside a linked
  // folder, which the item follows instead.
  ProfileId profile;
  std::int64_t added = 0;                 // Seconds since 1970.
  std::vector<BookmarkId> children = {};  // Folders only, in order.
};

// What Remove took away, so that it can be undone.
struct BookmarkRemoval {
  BookmarkId parent = 0;
  std::size_t index = 0;
  BookmarkId id = 0;
  std::vector<BookmarkItem> items = {};  // The item and everything inside.
};

// A folder for pickers, in tree order.
struct BookmarkFolderEntry {
  BookmarkId id = 0;
  int depth = 0;  // 0 for the top-level folders.
};

// The one bookmark tree of the app (011): "Bookmarks bar" and "Other
// bookmarks" with nested folders and bookmarks, each either general or
// linked to a profile. A folder's link applies to everything inside it:
// the model keeps items inside a linked folder unlinked themselves, so
// moving them out makes them general again. Not thread-safe; the UI thread
// owns it.
class BookmarkModel {
 public:
  static constexpr BookmarkId kBarId = 1;
  static constexpr BookmarkId kOtherId = 2;
  static constexpr std::size_t kEnd = static_cast<std::size_t>(-1);
  // Longer names and URLs are rejected (keeps the file and menus sane).
  static constexpr std::size_t kMaxNameBytes = 1024;
  static constexpr std::size_t kMaxUrlBytes = std::size_t{32} * 1024;

  BookmarkModel();

  const BookmarkItem* Find(BookmarkId id) const;
  static bool IsRoot(BookmarkId id) { return id == kBarId || id == kOtherId; }

  // Adds at `index` of `parent` (kEnd: at the end, as Chrome adds new
  // bookmarks). A link is dropped when `parent` is inside a linked folder.
  Result<BookmarkId> AddBookmark(BookmarkId parent,
                                 std::size_t index,
                                 std::string name,
                                 std::string url,
                                 ProfileId profile,
                                 std::int64_t added);
  Result<BookmarkId> AddFolder(BookmarkId parent,
                               std::size_t index,
                               std::string name,
                               ProfileId profile,
                               std::int64_t added);

  std::optional<Error> Rename(BookmarkId id, std::string name);
  std::optional<Error> SetUrl(BookmarkId id, std::string url);
  // Fails for the top-level folders and for items inside a linked folder.
  // Linking a folder unlinks everything inside it.
  std::optional<Error> SetProfile(BookmarkId id, ProfileId profile);

  // Moves to `index` of `parent`, counted as the children are before the
  // move (as Chrome does when dragging). Moving into a linked folder drops
  // the item's own links.
  std::optional<Error> Move(BookmarkId id,
                            BookmarkId parent,
                            std::size_t index);

  // Removes the item and everything inside it.
  std::optional<BookmarkRemoval> Remove(BookmarkId id);
  // Puts a removal back with the same ids: at its old place, or at the end
  // of "Other bookmarks" when its folder is gone. Returns the item's id.
  BookmarkId Restore(const BookmarkRemoval& removal);

  // The link of the nearest linked folder above `id`, else "" (then the
  // item's own link applies). Dialogs lock the profile when it is not "".
  ProfileId InheritedProfile(BookmarkId id) const;
  // The profile the item belongs to: inherited, else its own; "" general.
  ProfileId EffectiveProfile(BookmarkId id) const;
  // Shown where the active tab is in `profile`: general, or linked to it.
  bool IsVisibleIn(BookmarkId id, const ProfileId& profile) const;
  // `id` is `ancestor` or inside it.
  bool IsInside(BookmarkId id, BookmarkId ancestor) const;

  // Bookmarks with exactly this URL visible in `profile`, in tree order.
  std::vector<BookmarkId> BookmarksWithUrl(std::string_view url,
                                           const ProfileId& profile) const;
  // Bookmarks visible in `profile` whose name or URL contains every word of
  // `query` (case-insensitive), in tree order, at most `limit`. An empty
  // `profile` searches all bookmarks.
  std::vector<BookmarkId> Search(std::string_view query,
                                 const ProfileId& profile,
                                 std::size_t limit) const;
  // Every folder in tree order (for the folder picker of the dialogs),
  // limited to those visible in `profile` unless it is empty.
  std::vector<BookmarkFolderEntry> Folders(const ProfileId& profile) const;

  // Items linked to `profile` by their own link (011: deleting a profile
  // makes them general; its dialog says how many).
  int CountLinkedTo(const ProfileId& profile) const;
  int UnlinkProfile(const ProfileId& profile);

  // Increases with every change, so views can tell they are stale.
  std::uint64_t revision() const { return revision_; }

  // The bookmarks.json document, and back. Reading skips invalid items and
  // fails only when the document is not a bookmark file at all.
  JsonValue ToJson() const;
  static Result<BookmarkModel> FromJson(const JsonValue& json);

 private:
  BookmarkItem* FindMutable(BookmarkId id);
  Result<BookmarkId> Add(BookmarkItem item, std::size_t index);
  void ClearLinksInside(BookmarkId id);
  void Collect(BookmarkId id, std::vector<BookmarkItem>& out) const;
  void Changed() { ++revision_; }

  std::map<BookmarkId, BookmarkItem> items_;
  BookmarkId next_id_ = 3;
  std::uint64_t revision_ = 0;
};

}  // namespace brouk

#endif  // BROUK_CORE_BOOKMARK_MODEL_H_
