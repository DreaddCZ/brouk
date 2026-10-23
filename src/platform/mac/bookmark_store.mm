#import "platform/mac/bookmark_store.h"

#include <filesystem>
#include <utility>

#include "core/bookmark_file.h"

NSNotificationName const BRBookmarksDidChangeNotification =
    @"BRBookmarksDidChangeNotification";

namespace {

// Changes come in bursts (dragging, multi-select); one write after them.
constexpr NSTimeInterval kSaveDelay = 1.0;

}  // namespace

@implementation BRBookmarkStore {
  std::filesystem::path _path;
  brouk::BookmarkModel _model;
  std::vector<brouk::BookmarkRemoval> _lastRemoval;
  NSTimer* _saveTimer;
}

- (instancetype)initWithFile:(NSString*)path {
  if ((self = [super init])) {
    _path = path.fileSystemRepresentation;
    brouk::LoadedBookmarks loaded = brouk::LoadBookmarks(_path);
    if (loaded.problem) {
      NSLog(@"Bookmarks: %s", loaded.problem->c_str());
    }
    _model = std::move(loaded.model);
    _lastUsedFolder = brouk::BookmarkModel::kBarId;
  }
  return self;
}

- (const brouk::BookmarkModel&)model {
  return _model;
}

- (void)change:(const std::function<void(brouk::BookmarkModel&)>&)change {
  std::uint64_t before = _model.revision();
  change(_model);
  if (_model.revision() != before) {
    [self changed];
  }
}

- (void)changed {
  if (!_model.Find(_lastUsedFolder)) {
    _lastUsedFolder = brouk::BookmarkModel::kBarId;
  }
  [_saveTimer invalidate];
  _saveTimer = [NSTimer scheduledTimerWithTimeInterval:kSaveDelay
                                                target:self
                                              selector:@selector(saveNow)
                                              userInfo:nil
                                               repeats:NO];
  [NSNotificationCenter.defaultCenter
      postNotificationName:BRBookmarksDidChangeNotification
                    object:self];
}

- (void)removeItems:(const std::vector<brouk::BookmarkId>&)ids {
  std::vector<brouk::BookmarkRemoval> removals;
  for (brouk::BookmarkId id : ids) {
    // An item inside another removed item went with it.
    if (std::optional<brouk::BookmarkRemoval> removal = _model.Remove(id)) {
      removals.push_back(std::move(*removal));
    }
  }
  if (removals.empty()) {
    return;
  }
  _lastRemoval = std::move(removals);
  [self changed];
}

- (BOOL)canUndoRemove {
  return !_lastRemoval.empty();
}

- (void)undoRemove {
  if (_lastRemoval.empty()) {
    return;
  }
  // Back in reverse order, so each goes back to its old index.
  for (auto it = _lastRemoval.rbegin(); it != _lastRemoval.rend(); ++it) {
    _model.Restore(*it);
  }
  _lastRemoval.clear();
  [self changed];
}

- (void)saveNow {
  if (!_saveTimer) {
    return;  // Nothing changed since the last write.
  }
  [_saveTimer invalidate];
  _saveTimer = nil;
  if (std::optional<brouk::Error> error = brouk::SaveBookmarks(_path, _model)) {
    NSLog(@"Bookmarks not saved: %s", error->message.c_str());
  }
}

@end
