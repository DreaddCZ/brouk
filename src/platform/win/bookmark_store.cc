#include "platform/win/bookmark_store.h"

#include <optional>
#include <utility>

#include "cef/function_task.h"
#include "core/bookmark_file.h"
#include "include/base/cef_logging.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk::win {

namespace {

// Changes come in bursts (dragging, multi-select); one write after them.
constexpr int64_t kSaveDelayMs = 1000;

}  // namespace

BookmarkStore::BookmarkStore(std::filesystem::path path)
    : path_(std::move(path)) {
  LoadedBookmarks loaded = LoadBookmarks(path_);
  if (loaded.problem) {
    LOG(WARNING) << "Bookmarks: " << *loaded.problem;
  }
  model_ = std::move(loaded.model);
}

BookmarkStore::~BookmarkStore() {
  SaveNow();
}

void BookmarkStore::Change(const std::function<void(BookmarkModel&)>& change) {
  const std::uint64_t before = model_.revision();
  change(model_);
  if (model_.revision() != before) {
    Changed();
  }
}

void BookmarkStore::Remove(const std::vector<BookmarkId>& ids) {
  std::vector<BookmarkRemoval> removals;
  for (BookmarkId id : ids) {
    // An item inside another removed item went with it.
    if (std::optional<BookmarkRemoval> removal = model_.Remove(id)) {
      removals.push_back(std::move(*removal));
    }
  }
  if (removals.empty()) {
    return;
  }
  last_removal_ = std::move(removals);
  Changed();
}

void BookmarkStore::UndoRemove() {
  if (last_removal_.empty()) {
    return;
  }
  // Back in reverse order, so each goes back to its old index.
  for (auto it = last_removal_.rbegin(); it != last_removal_.rend(); ++it) {
    model_.Restore(*it);
  }
  last_removal_.clear();
  Changed();
}

int BookmarkStore::Watch(std::function<void()> changed) {
  const int id = next_watcher_++;
  watchers_[id] = std::move(changed);
  return id;
}

void BookmarkStore::Unwatch(int id) {
  watchers_.erase(id);
}

void BookmarkStore::Changed() {
  CEF_REQUIRE_UI_THREAD();
  if (!model_.Find(last_used_folder_)) {
    last_used_folder_ = BookmarkModel::kBarId;
  }
  save_pending_ = true;
  const int generation = ++save_generation_;
  std::weak_ptr<int> alive = alive_;
  CefPostDelayedTask(TID_UI, new FunctionTask([this, alive, generation] {
                       if (!alive.expired() && generation == save_generation_) {
                         SaveNow();
                       }
                     }),
                     kSaveDelayMs);
  // A copy: a watcher may stop watching while it is told.
  std::vector<std::function<void()>> watchers;
  for (const auto& [id, changed] : watchers_) {
    watchers.push_back(changed);
  }
  for (const std::function<void()>& changed : watchers) {
    changed();
  }
}

void BookmarkStore::SaveNow() {
  if (!save_pending_) {
    return;  // Nothing changed since the last write.
  }
  save_pending_ = false;
  if (std::optional<Error> error = SaveBookmarks(path_, model_)) {
    LOG(ERROR) << "Bookmarks not saved: " << error->message;
  }
}

}  // namespace brouk::win
