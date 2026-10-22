#include "core/tab_strip_model.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace brouk {

TabStripModel::TabStripModel(ProfileId default_profile)
    : default_profile_(std::move(default_profile)) {}

std::size_t TabStripModel::AppendTab(TabId id,
                                     const ProfileId& profile,
                                     bool activate) {
  if (profile != default_profile_ &&
      std::ranges::find(group_order_, profile) == group_order_.end()) {
    group_order_.push_back(profile);
  }
  return InsertAt(GroupEnd(profile), {id, profile, std::nullopt}, activate);
}

std::size_t TabStripModel::AppendTab(TabId id, bool activate) {
  return AppendTab(id, default_profile_, activate);
}

std::size_t TabStripModel::InsertFromOpener(TabId id,
                                            TabId opener,
                                            bool activate) {
  std::optional<std::size_t> opener_index = IndexOf(opener);
  if (!opener_index) {
    return AppendTab(id, activate);
  }
  // Tabs opened from the opener are in its group, right after it.
  std::size_t index = *opener_index + 1;
  while (index < tabs_.size() && tabs_[index].opener == opener) {
    ++index;
  }
  return InsertAt(index, {id, tabs_[*opener_index].profile, opener}, activate);
}

std::size_t TabStripModel::InsertAt(std::size_t index,
                                    Entry entry,
                                    bool activate) {
  if (activate) {
    collapsed_.erase(entry.profile);
  }
  tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(index),
               std::move(entry));
  if (active_index_ && *active_index_ >= index) {
    ++*active_index_;
  }
  if (activate || !active_index_) {
    active_index_ = index;
    collapsed_.erase(tabs_[index].profile);
  }
  return index;
}

bool TabStripModel::RemoveTab(TabId id) {
  std::optional<std::size_t> found = IndexOf(id);
  if (!found) {
    return false;
  }
  std::size_t index = *found;
  std::optional<TabId> opener = tabs_[index].opener;
  ProfileId profile = tabs_[index].profile;
  bool was_active = active_index_ == index;

  // Pick the successor while the closing tab is still in the list.
  std::optional<TabId> next_active;
  if (was_active && opener) {
    // Another tab from the same opener: the nearest one to the right first,
    // then to the left (Chrome's "GetIndexOfNextWebContentsOpenedBy").
    for (std::size_t i = index + 1; i < tabs_.size() && !next_active; ++i) {
      if (tabs_[i].opener == opener) {
        next_active = tabs_[i].id;
      }
    }
    for (std::size_t i = index; i > 0 && !next_active; --i) {
      if (tabs_[i - 1].opener == opener) {
        next_active = tabs_[i - 1].id;
      }
    }
    if (!next_active && IndexOf(*opener)) {
      next_active = opener;
    }
  }

  tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(index));
  // Tabs that were opened by the closed tab no longer have an opener.
  for (Entry& entry : tabs_) {
    if (entry.opener == id) {
      entry.opener.reset();
    }
  }
  // A group disappears with its last tab (except the default profile's).
  if (std::ranges::none_of(
          tabs_, [&](const Entry& e) { return e.profile == profile; })) {
    std::erase(group_order_, profile);
    collapsed_.erase(profile);
  }

  if (tabs_.empty()) {
    active_index_.reset();
    return true;
  }
  if (!was_active) {
    if (active_index_ && *active_index_ > index) {
      --*active_index_;
    }
    return true;
  }
  std::optional<std::size_t> next =
      next_active ? IndexOf(*next_active) : std::nullopt;
  if (!next || IsCollapsedAt(*next)) {
    // The tab to the right moved into `index`; then the tabs to the left.
    // Tabs in collapsed groups stay hidden if any other tab is visible.
    std::size_t start = std::min(index, tabs_.size() - 1);
    std::optional<std::size_t> visible;
    for (std::size_t i = start; i < tabs_.size() && !visible; ++i) {
      if (!IsCollapsedAt(i)) {
        visible = i;
      }
    }
    for (std::size_t i = start; i > 0 && !visible; --i) {
      if (!IsCollapsedAt(i - 1)) {
        visible = i - 1;
      }
    }
    if (visible) {
      next = visible;
    } else if (!next) {
      next = start;
    }
  }
  active_index_ = next;
  collapsed_.erase(tabs_[*next].profile);
  return true;
}

bool TabStripModel::ActivateTab(TabId id) {
  std::optional<std::size_t> index = IndexOf(id);
  return index && ActivateIndex(*index);
}

bool TabStripModel::ActivateIndex(std::size_t index) {
  if (index >= tabs_.size()) {
    return false;
  }
  collapsed_.erase(tabs_[index].profile);
  if (active_index_ != index) {
    ForgetOpeners();
    active_index_ = index;
  }
  return true;
}

void TabStripModel::ActivateNext() {
  if (tabs_.empty() || !active_index_) {
    return;
  }
  for (std::size_t step = 1; step < tabs_.size(); ++step) {
    std::size_t index = (*active_index_ + step) % tabs_.size();
    if (!IsCollapsedAt(index)) {
      ActivateIndex(index);
      return;
    }
  }
}

void TabStripModel::ActivatePrevious() {
  if (tabs_.empty() || !active_index_) {
    return;
  }
  for (std::size_t step = 1; step < tabs_.size(); ++step) {
    std::size_t index = (*active_index_ + tabs_.size() - step) % tabs_.size();
    if (!IsCollapsedAt(index)) {
      ActivateIndex(index);
      return;
    }
  }
}

void TabStripModel::ActivateByShortcutNumber(int number) {
  if (tabs_.empty() || number < 1 || number > 9) {
    return;
  }
  if (number == 9) {
    ActivateIndex(tabs_.size() - 1);
  } else {
    ActivateIndex(static_cast<std::size_t>(number - 1));
  }
}

bool TabStripModel::MoveTab(TabId id, std::size_t to_index) {
  std::optional<std::size_t> from = IndexOf(id);
  if (!from) {
    return false;
  }
  const ProfileId profile = tabs_[*from].profile;
  std::size_t end = GroupEnd(profile);
  std::size_t start = end;
  while (start > 0 && tabs_[start - 1].profile == profile) {
    --start;
  }
  to_index = std::clamp(to_index, start, end - 1);
  if (to_index == *from) {
    return true;
  }
  std::optional<TabId> active = active_id();
  Entry entry = tabs_[*from];
  // Moving a tab is a user action that breaks Chrome's opener relation.
  entry.opener.reset();
  tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(*from));
  tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(to_index),
               std::move(entry));
  if (active) {
    active_index_ = IndexOf(*active);
  }
  return true;
}

bool TabStripModel::SetGroupCollapsed(std::string_view profile,
                                      bool collapsed) {
  auto first = std::ranges::find(tabs_, profile, &Entry::profile);
  if (first == tabs_.end()) {
    return false;  // No tabs: nothing to collapse.
  }
  bool is_collapsed = collapsed_.contains(profile);
  if (collapsed == is_collapsed) {
    return false;
  }
  if (!collapsed) {
    collapsed_.erase(collapsed_.find(profile));
    return true;
  }
  if (active_index_ && tabs_[*active_index_].profile == profile) {
    // Like Chrome: the nearest visible tab outside the group, to the right
    // first.
    auto start = static_cast<std::size_t>(std::distance(tabs_.begin(), first));
    std::size_t end = GroupEnd(ProfileId(profile));
    std::optional<std::size_t> other;
    for (std::size_t i = end; i < tabs_.size() && !other; ++i) {
      if (!IsCollapsedAt(i)) {
        other = i;
      }
    }
    for (std::size_t i = start; i > 0 && !other; --i) {
      if (!IsCollapsedAt(i - 1)) {
        other = i - 1;
      }
    }
    if (!other) {
      return false;
    }
    ActivateIndex(*other);
  }
  collapsed_.emplace(profile);
  return true;
}

void TabStripModel::SetDefaultProfile(const ProfileId& profile) {
  if (profile == default_profile_) {
    return;
  }
  bool old_has_tabs = std::ranges::any_of(
      tabs_, [&](const Entry& e) { return e.profile == default_profile_; });
  std::erase(group_order_, profile);
  if (old_has_tabs) {
    group_order_.push_back(default_profile_);
  }
  default_profile_ = profile;
  ArrangeGroups();
}

std::vector<TabStripModel::Group> TabStripModel::groups() const {
  std::vector<Group> groups;
  for (const ProfileId& profile : group_order_) {
    Group group{profile, GroupEnd(profile), 0, collapsed_.contains(profile)};
    while (group.start > 0 && tabs_[group.start - 1].profile == profile) {
      --group.start;
      ++group.count;
    }
    groups.push_back(group);
  }
  Group last{default_profile_, tabs_.size(), 0,
             collapsed_.contains(default_profile_)};
  while (last.start > 0 && tabs_[last.start - 1].profile == default_profile_) {
    --last.start;
    ++last.count;
  }
  groups.push_back(last);
  return groups;
}

std::optional<std::size_t> TabStripModel::IndexOf(TabId id) const {
  auto it = std::ranges::find(tabs_, id, &Entry::id);
  if (it == tabs_.end()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(std::distance(tabs_.begin(), it));
}

std::optional<TabId> TabStripModel::active_id() const {
  if (!active_index_) {
    return std::nullopt;
  }
  return tabs_[*active_index_].id;
}

std::optional<TabId> TabStripModel::OpenerOf(TabId id) const {
  std::optional<std::size_t> index = IndexOf(id);
  if (!index) {
    return std::nullopt;
  }
  return tabs_[*index].opener;
}

const ProfileId* TabStripModel::ProfileOf(TabId id) const {
  std::optional<std::size_t> index = IndexOf(id);
  return index ? &tabs_[*index].profile : nullptr;
}

std::size_t TabStripModel::GroupEnd(const ProfileId& profile) const {
  if (profile == default_profile_) {
    return tabs_.size();
  }
  // Groups are contiguous: one past the last tab of the profile, or, for a
  // new group, where the default profile's group starts.
  for (std::size_t i = tabs_.size(); i > 0; --i) {
    if (tabs_[i - 1].profile == profile) {
      return i;
    }
  }
  auto first_default =
      std::ranges::find(tabs_, default_profile_, &Entry::profile);
  return static_cast<std::size_t>(std::distance(tabs_.begin(), first_default));
}

bool TabStripModel::IsCollapsedAt(std::size_t index) const {
  return collapsed_.contains(tabs_[index].profile);
}

void TabStripModel::ForgetOpeners() {
  for (Entry& entry : tabs_) {
    entry.opener.reset();
  }
}

void TabStripModel::ArrangeGroups() {
  std::optional<TabId> active = active_id();
  auto rank = [this](const Entry& entry) {
    auto it = std::ranges::find(group_order_, entry.profile);
    return static_cast<std::size_t>(std::distance(group_order_.begin(), it));
  };
  std::ranges::stable_sort(tabs_, {}, rank);
  if (active) {
    active_index_ = IndexOf(*active);
  }
}

}  // namespace brouk
