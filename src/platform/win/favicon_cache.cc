#include "platform/win/favicon_cache.h"

#include "cef/function_task.h"
#include "cef/history_service.h"
#include "core/adblock_policy.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk::win {

FaviconCache::FaviconCache(HistoryService* history) : history_(history) {}

void FaviconCache::Detach() {
  history_ = nullptr;
  pending_.clear();
}

const Favicon* FaviconCache::IconFor(const ProfileId& profile,
                                     const std::string& url,
                                     int size) {
  CEF_REQUIRE_UI_THREAD();
  if (url.empty()) {
    return nullptr;
  }
  auto [it, added] = entries_.try_emplace(Key{profile, url});
  Entry& entry = it->second;
  if (added) {
    Fetch(profile, url);
    return nullptr;
  }
  if (entry.png.empty()) {
    return nullptr;
  }
  if (entry.size != size || entry.icon.empty()) {
    entry.icon = Favicon::FromPng(entry.png, size);
    entry.size = size;
  }
  return entry.icon.empty() ? nullptr : &entry.icon;
}

Favicon FaviconCache::CopyOf(const ProfileId& profile,
                             const std::string& url,
                             int size) {
  CEF_REQUIRE_UI_THREAD();
  if (url.empty()) {
    return {};
  }
  auto [it, added] = entries_.try_emplace(Key{profile, url});
  if (added) {
    Fetch(profile, url);
    return {};
  }
  return it->second.png.empty() ? Favicon()
                                : Favicon::FromPng(it->second.png, size);
}

void FaviconCache::IconChanged(const ProfileId& profile,
                               const std::string& url) {
  CEF_REQUIRE_UI_THREAD();
  const std::string host = HostOfUrl(url);
  for (auto& [key, entry] : entries_) {
    if (key.first != profile || entry.loading) {
      continue;
    }
    // A page without an icon of its own shows its site's.
    if (key.second == url ||
        (entry.png.empty() && !host.empty() && HostOfUrl(key.second) == host)) {
      Fetch(key.first, key.second);
    }
  }
}

int FaviconCache::Watch(std::function<void()> changed) {
  const int id = next_watcher_++;
  watchers_[id] = std::move(changed);
  return id;
}

void FaviconCache::Unwatch(int id) {
  watchers_.erase(id);
}

void FaviconCache::Fetch(const ProfileId& profile, const std::string& url) {
  if (!history_) {
    return;
  }
  entries_[Key{profile, url}].loading = true;
  pending_[profile].push_back(url);
  if (flush_posted_) {
    return;
  }
  flush_posted_ = true;
  std::weak_ptr<int> alive = alive_;
  CefPostTask(TID_UI, new FunctionTask([this, alive] {
                if (!alive.expired()) {
                  Flush();
                }
              }));
}

void FaviconCache::Flush() {
  flush_posted_ = false;
  std::map<ProfileId, std::vector<std::string>> pending;
  pending.swap(pending_);
  if (!history_) {
    return;
  }
  for (auto& [profile, urls] : pending) {
    std::weak_ptr<int> alive = alive_;
    history_->Favicons(
        profile, urls,
        [this, alive, profile,
         asked = urls](std::map<std::string, std::vector<std::uint8_t>> icons) {
          if (!alive.expired()) {
            Received(profile, asked, icons);
          }
        });
  }
}

void FaviconCache::Received(
    const ProfileId& profile,
    const std::vector<std::string>& asked,
    const std::map<std::string, std::vector<std::uint8_t>>& icons) {
  bool changed = false;
  for (const std::string& url : asked) {
    Entry& entry = entries_[Key{profile, url}];
    entry.loading = false;
    const auto icon = icons.find(url);
    if (icon == icons.end()) {
      changed = changed || !entry.png.empty();
      entry.png.clear();
      entry.icon = Favicon();
      continue;
    }
    if (icon->second == entry.png) {
      continue;
    }
    entry.png = icon->second;
    entry.icon = Favicon();  // Decoded when next drawn, at its size.
    changed = true;
  }
  if (!changed) {
    return;
  }
  // A copy: a watcher may unwatch while it runs.
  const std::map<int, std::function<void()>> watchers = watchers_;
  for (const auto& [id, watcher] : watchers) {
    if (watchers_.contains(id)) {
      watcher();
    }
  }
}

}  // namespace brouk::win
