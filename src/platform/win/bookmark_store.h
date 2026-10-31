#ifndef BROUK_PLATFORM_WIN_BOOKMARK_STORE_H_
#define BROUK_PLATFORM_WIN_BOOKMARK_STORE_H_

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "core/bookmark_model.h"

namespace brouk::win {

// The app's bookmarks (011), as on macOS (BRBookmarkStore): the core's
// BookmarkModel, loaded from bookmarks.json at start and saved a moment
// after each change. Every view (menu, star, bar, manager page) changes
// bookmarks through this object and follows them by watching it. UI thread.
class BookmarkStore {
 public:
  explicit BookmarkStore(std::filesystem::path path);
  // Writes pending changes.
  ~BookmarkStore();
  BookmarkStore(const BookmarkStore&) = delete;
  BookmarkStore& operator=(const BookmarkStore&) = delete;

  const BookmarkModel& model() const { return model_; }

  // Runs `change` on the model; when it changed anything, watchers are
  // told and the file is saved soon.
  void Change(const std::function<void(BookmarkModel&)>& change);

  // Removes items (011: any deletion can be undone); the last removal can
  // be put back with UndoRemove.
  void Remove(const std::vector<BookmarkId>& ids);
  bool CanUndoRemove() const { return !last_removal_.empty(); }
  void UndoRemove();

  // The folder the add dialog offers first (Chrome: the last one used);
  // the bar until something else was chosen.
  BookmarkId last_used_folder() const { return last_used_folder_; }
  void set_last_used_folder(BookmarkId folder) { last_used_folder_ = folder; }

  // `changed` runs after every change, until Unwatch with the returned id.
  int Watch(std::function<void()> changed);
  void Unwatch(int id);

  // Writes pending changes now (at quit).
  void SaveNow();

 private:
  void Changed();

  const std::filesystem::path path_;
  BookmarkModel model_;
  std::vector<BookmarkRemoval> last_removal_;
  BookmarkId last_used_folder_ = BookmarkModel::kBarId;
  bool save_pending_ = false;
  // Only the newest delayed save writes.
  int save_generation_ = 0;
  std::map<int, std::function<void()>> watchers_;
  int next_watcher_ = 1;
  // Expires with the store, for the delayed save.
  std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_BOOKMARK_STORE_H_
