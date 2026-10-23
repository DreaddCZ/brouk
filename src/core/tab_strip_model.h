#ifndef BROUK_CORE_TAB_STRIP_MODEL_H_
#define BROUK_CORE_TAB_STRIP_MODEL_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

#include "core/profile_id.h"

namespace brouk {

using TabId = std::int64_t;

// Order, selection and profile groups of the tabs in one window, independent
// of any UI or CEF. Implements Chrome's rules for where new tabs go and which
// tab becomes active when the active tab closes, so the platform UI only
// renders it.
//
// Profile groups (003, 005): all tabs of one profile form exactly one group.
// The default profile's group is always last (rightmost) and always exists,
// even without tabs; other groups appear with their first tab, in the order
// they appeared, and disappear with their last.
//
// Pinned tabs (003) work as in Chrome, except that they sit at the start of
// their own profile group instead of at the start of the strip.
class TabStripModel {
 public:
  explicit TabStripModel(ProfileId default_profile);
  TabStripModel(const TabStripModel&) = delete;
  TabStripModel& operator=(const TabStripModel&) = delete;

  struct Group {
    ProfileId profile;
    std::size_t start = 0;  // Index of its first tab.
    std::size_t count = 0;  // 0 only for the default profile's group.
    std::size_t pinned_count = 0;  // Its first `pinned_count` tabs.
    bool collapsed = false;
  };

  // Appends a tab at the end of `profile`'s group (Cmd+T, a group's "+")
  // and returns its index.
  std::size_t AppendTab(TabId id, const ProfileId& profile, bool activate);
  // The same, in the default profile.
  std::size_t AppendTab(TabId id, bool activate);

  // Inserts a tab opened from `opener` (a link, a popup) in the opener's
  // profile, right after the opener and after tabs opened from it earlier,
  // as Chrome does; never among the group's pinned tabs. Falls back to appending in the default profile if the
  // opener is not in this strip. Returns its index.
  std::size_t InsertFromOpener(TabId id, TabId opener, bool activate);

  // Removes a tab. If it was active, picks the next active tab: another tab
  // opened by the same opener, then the opener, then the tab to the right,
  // then the one to the left; tabs in collapsed groups only if there is no
  // other. Returns false if the id is unknown.
  bool RemoveTab(TabId id);

  // Activates a tab chosen by the user, expanding its group. Like Chrome,
  // this forgets opener relations, so closing the tab later does not jump
  // back to an old opener.
  bool ActivateTab(TabId id);
  bool ActivateIndex(std::size_t index);
  // Cmd+Opt+Right / Ctrl+Tab and Cmd+Opt+Left / Ctrl+Shift+Tab; wraps
  // around and skips collapsed groups.
  void ActivateNext();
  void ActivatePrevious();
  // Cmd+1 ... Cmd+8 activate tab 1..8 (if it exists); Cmd+9 the last tab.
  void ActivateByShortcutNumber(int number);

  // Moves a tab to `to_index`, kept inside its own group (dragging a tab to
  // another profile reopens it there instead, see 005), and pinned tabs
  // among the pinned ones, others after them.
  bool MoveTab(TabId id, std::size_t to_index);

  // Pins a tab (it moves to the end of its group's pinned tabs) or unpins it
  // (it moves to the first place after them), as Chrome does. Returns
  // whether anything changed.
  bool SetPinned(TabId id, bool pinned);
  bool IsPinned(TabId id) const;

  // Collapses or expands a group (clicking its label). Collapsing the group
  // of the active tab activates the nearest tab outside it; if there is none,
  // the group stays expanded. Returns whether the state changed.
  bool SetGroupCollapsed(std::string_view profile, bool collapsed);

  // The default profile changed (settings): its group moves to the end.
  void SetDefaultProfile(const ProfileId& profile);
  // The tab of `profile` that was active most recently, else its first tab;
  // nullopt if it has no tabs. Clicking a group the strip collapsed for lack
  // of space switches to it.
  std::optional<TabId> LastActiveTabOf(std::string_view profile) const;
  const ProfileId& default_profile() const { return default_profile_; }

  // Left to right, always ending with the default profile's group.
  std::vector<Group> groups() const;

  std::size_t count() const { return tabs_.size(); }
  bool empty() const { return tabs_.empty(); }
  TabId id_at(std::size_t index) const { return tabs_[index].id; }
  std::optional<std::size_t> IndexOf(TabId id) const;
  std::optional<TabId> active_id() const;
  std::optional<std::size_t> active_index() const { return active_index_; }
  std::optional<TabId> OpenerOf(TabId id) const;
  // Null for unknown tabs.
  const ProfileId* ProfileOf(TabId id) const;

 private:
  struct Entry {
    TabId id;
    ProfileId profile;
    std::optional<TabId> opener;
    bool pinned = false;
    // Increases each time a tab becomes active (0: never was).
    std::uint64_t activated = 0;
  };

  // Where `profile`'s group ends (one past its last tab); for a profile
  // without tabs, where its group would start.
  std::size_t GroupEnd(const ProfileId& profile) const;
  std::size_t GroupStart(const ProfileId& profile) const;
  // One past the group's last pinned tab.
  std::size_t PinnedEnd(const ProfileId& profile) const;
  // Makes the tab at `index` active and remembers when.
  void SetActiveIndex(std::size_t index);
  // Moves the tab at `from` to `to` (both valid indices), keeping the active
  // tab active.
  void Relocate(std::size_t from, std::size_t to);
  std::size_t InsertAt(std::size_t index, Entry entry, bool activate);
  bool IsCollapsedAt(std::size_t index) const;
  void ForgetOpeners();
  // Reorders tabs_ to follow group_order_ (stable inside groups).
  void ArrangeGroups();

  ProfileId default_profile_;
  std::vector<Entry> tabs_;
  std::optional<std::size_t> active_index_;
  // Non-default groups in the order they appeared.
  std::vector<ProfileId> group_order_;
  std::set<ProfileId, std::less<>> collapsed_;
  std::uint64_t activation_count_ = 0;
};

}  // namespace brouk

#endif  // BROUK_CORE_TAB_STRIP_MODEL_H_
