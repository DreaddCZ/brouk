#ifndef BROUK_CORE_TAB_STRIP_MODEL_H_
#define BROUK_CORE_TAB_STRIP_MODEL_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace brouk {

using TabId = std::int64_t;

// Order and selection of the tabs in one window, independent of any UI or
// CEF. Implements Chrome's rules for where new tabs go and which tab becomes
// active when the active tab closes, so the platform UI only renders it.
//
// Profile groups (003, 005) are added on top of this model in milestone 3.
class TabStripModel {
 public:
  TabStripModel() = default;
  TabStripModel(const TabStripModel&) = delete;
  TabStripModel& operator=(const TabStripModel&) = delete;

  // Appends a tab at the end (Cmd+T) and returns its index.
  std::size_t AppendTab(TabId id, bool activate);

  // Inserts a tab opened from `opener` (a link, a popup) right after the
  // opener and after tabs opened from it earlier, as Chrome does. Falls back
  // to appending if the opener is not in this strip. Returns its index.
  std::size_t InsertFromOpener(TabId id, TabId opener, bool activate);

  // Removes a tab. If it was active, picks the next active tab: another tab
  // opened by the same opener, then the opener, then the tab to the right,
  // then the one to the left. Returns false if the id is unknown.
  bool RemoveTab(TabId id);

  // Activates a tab chosen by the user. Like Chrome, this forgets opener
  // relations, so closing the tab later does not jump back to an old opener.
  bool ActivateTab(TabId id);
  bool ActivateIndex(std::size_t index);
  // Cmd+Opt+Right / Ctrl+Tab and Cmd+Opt+Left / Ctrl+Shift+Tab; wraps around.
  void ActivateNext();
  void ActivatePrevious();
  // Cmd+1 ... Cmd+8 activate tab 1..8 (if it exists); Cmd+9 the last tab.
  void ActivateByShortcutNumber(int number);

  // Moves a tab to `to_index` (clamped to the end).
  bool MoveTab(TabId id, std::size_t to_index);

  std::size_t count() const { return tabs_.size(); }
  bool empty() const { return tabs_.empty(); }
  TabId id_at(std::size_t index) const { return tabs_[index].id; }
  std::optional<std::size_t> IndexOf(TabId id) const;
  std::optional<TabId> active_id() const;
  std::optional<std::size_t> active_index() const { return active_index_; }
  std::optional<TabId> OpenerOf(TabId id) const;

 private:
  struct Entry {
    TabId id;
    std::optional<TabId> opener;
  };

  void ForgetOpeners();

  std::vector<Entry> tabs_;
  std::optional<std::size_t> active_index_;
};

}  // namespace brouk

#endif  // BROUK_CORE_TAB_STRIP_MODEL_H_
