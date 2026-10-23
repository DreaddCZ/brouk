#include "core/closed_tabs.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace brouk {

namespace {

bool IsWorthKeeping(const ClosedTab& tab) {
  return !tab.url.empty() && tab.url != "about:blank";
}

}  // namespace

void ClosedTabList::AddTab(ClosedTab tab) {
  if (!IsWorthKeeping(tab)) {
    return;
  }
  ClosedEntry entry;
  entry.window_id = tab.window_id;
  entry.tabs.push_back(std::move(tab));
  Add(std::move(entry));
}

void ClosedTabList::AddWindow(std::int64_t window_id,
                              std::vector<ClosedTab> tabs) {
  std::erase_if(tabs,
                [](const ClosedTab& tab) { return !IsWorthKeeping(tab); });
  if (tabs.empty()) {
    return;
  }
  ClosedEntry entry;
  entry.is_window = true;
  entry.window_id = window_id;
  entry.tabs = std::move(tabs);
  Add(std::move(entry));
}

void ClosedTabList::Add(ClosedEntry entry) {
  entry.id = next_id_++;
  entries_.push_front(std::move(entry));
  if (entries_.size() > kMaxEntries) {
    entries_.pop_back();
  }
}

std::optional<ClosedEntry> ClosedTabList::TakeNewest() {
  if (entries_.empty()) {
    return std::nullopt;
  }
  ClosedEntry entry = std::move(entries_.front());
  entries_.pop_front();
  return entry;
}

std::optional<ClosedEntry> ClosedTabList::Take(std::int64_t id) {
  auto it = std::ranges::find(entries_, id, &ClosedEntry::id);
  if (it == entries_.end()) {
    return std::nullopt;
  }
  ClosedEntry entry = std::move(*it);
  entries_.erase(it);
  return entry;
}

void ClosedTabList::RemoveProfile(std::string_view profile) {
  for (ClosedEntry& entry : entries_) {
    std::erase_if(entry.tabs, [profile](const ClosedTab& tab) {
      return tab.profile == profile;
    });
  }
  std::erase_if(entries_,
                [](const ClosedEntry& entry) { return entry.tabs.empty(); });
}

}  // namespace brouk
