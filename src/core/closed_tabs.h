#ifndef BROUK_CORE_CLOSED_TABS_H_
#define BROUK_CORE_CLOSED_TABS_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/profile_id.h"

namespace brouk {

// A closed tab, enough to open it again (013: Reopen Closed Tab). CEF cannot
// restore a tab's back/forward list, so only its page comes back.
struct ClosedTab {
  std::string url;
  std::string title;
  ProfileId profile;
  bool pinned = false;
  std::int64_t window_id = 0;  // The window it was in.
  std::size_t position = 0;    // Its place in its profile group.
};

// A closed tab, or a window closed with all its tabs.
struct ClosedEntry {
  std::int64_t id = 0;  // Unique in this run (for the menu).
  bool is_window = false;
  std::int64_t window_id = 0;
  std::vector<ClosedTab> tabs;  // One for a tab; left to right for a window.
};

// The tabs and windows closed in this run, newest first, at most 10 (013).
// Kept until the app quits.
class ClosedTabList {
 public:
  static constexpr std::size_t kMaxEntries = 10;

  // New tab pages (empty or about:blank) are not kept, as in Chrome.
  void AddTab(ClosedTab tab);
  // A window with its tabs; nothing if none of them is worth keeping.
  void AddWindow(std::int64_t window_id, std::vector<ClosedTab> tabs);

  // Removes and returns the newest entry (⇧⌘T).
  std::optional<ClosedEntry> TakeNewest();
  // Removes and returns the entry with `id` (chosen in the menu).
  std::optional<ClosedEntry> Take(std::int64_t id);
  // A deleted profile's tabs are gone for good (005).
  void RemoveProfile(std::string_view profile);

  const std::deque<ClosedEntry>& entries() const { return entries_; }
  bool empty() const { return entries_.empty(); }

 private:
  void Add(ClosedEntry entry);

  std::deque<ClosedEntry> entries_;  // Newest first.
  std::int64_t next_id_ = 1;
};

}  // namespace brouk

#endif  // BROUK_CORE_CLOSED_TABS_H_
