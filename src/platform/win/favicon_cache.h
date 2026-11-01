#ifndef BROUK_PLATFORM_WIN_FAVICON_CACHE_H_
#define BROUK_PLATFORM_WIN_FAVICON_CACHE_H_

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/profile_id.h"
#include "platform/win/favicon.h"

namespace brouk {
class HistoryService;
}

namespace brouk::win {

// Site icons for Brouk's own views (013): the bookmarks bar and the
// bookmark menus, as on macOS. Icons come from each profile's history;
// this keeps the ones views asked for, so drawing never waits for the
// disk. UI thread.
class FaviconCache {
 public:
  // `history` must outlive the cache, or be detached first.
  explicit FaviconCache(HistoryService* history);
  FaviconCache(const FaviconCache&) = delete;
  FaviconCache& operator=(const FaviconCache&) = delete;

  // The icon of `url` in `profile`'s history at `size` pixels, else null
  // (not known yet, or none). Unknown icons are fetched; the watchers are
  // told when they arrive.
  const Favicon* IconFor(const ProfileId& profile,
                         const std::string& url,
                         int size);
  // The same as a decoded copy the caller keeps, for menus: the cache may
  // replace its own while a menu shows.
  Favicon CopyOf(const ProfileId& profile, const std::string& url, int size);
  // A page of `profile` got an icon: icons of its address and its site
  // that views asked for are fetched again.
  void IconChanged(const ProfileId& profile, const std::string& url);
  // `changed` runs when icons arrived or changed (redraw); returns an id
  // for Unwatch.
  int Watch(std::function<void()> changed);
  void Unwatch(int id);
  // The history is going away (quit).
  void Detach();

 private:
  using Key = std::pair<ProfileId, std::string>;
  struct Entry {
    std::vector<std::uint8_t> png;  // Empty: none, or not known yet.
    Favicon icon;                   // Decoded at `size`.
    int size = 0;
    bool loading = false;
  };

  void Fetch(const ProfileId& profile, const std::string& url);
  void Flush();
  void Received(const ProfileId& profile,
                const std::vector<std::string>& asked,
                const std::map<std::string, std::vector<std::uint8_t>>& icons);

  HistoryService* history_;  // Not owned; null once detached.
  std::map<Key, Entry> entries_;
  // URLs to fetch per profile, sent together in one task.
  std::map<ProfileId, std::vector<std::string>> pending_;
  bool flush_posted_ = false;
  std::map<int, std::function<void()>> watchers_;
  int next_watcher_ = 1;
  // Expires with the cache, for answers that come later.
  std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_FAVICON_CACHE_H_
