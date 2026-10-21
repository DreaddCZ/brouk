#include "core/tab_strip_model.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>

namespace brouk {

std::size_t TabStripModel::AppendTab(TabId id, bool activate) {
  tabs_.push_back({id, std::nullopt});
  std::size_t index = tabs_.size() - 1;
  if (activate || !active_index_) {
    active_index_ = index;
  }
  return index;
}

std::size_t TabStripModel::InsertFromOpener(TabId id,
                                            TabId opener,
                                            bool activate) {
  std::optional<std::size_t> opener_index = IndexOf(opener);
  if (!opener_index) {
    return AppendTab(id, activate);
  }
  std::size_t index = *opener_index + 1;
  while (index < tabs_.size() && tabs_[index].opener == opener) {
    ++index;
  }
  tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(index),
               {id, opener});
  if (active_index_ && *active_index_ >= index) {
    ++*active_index_;
  }
  if (activate || !active_index_) {
    active_index_ = index;
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

  if (tabs_.empty()) {
    active_index_.reset();
  } else if (was_active) {
    if (next_active) {
      active_index_ = IndexOf(*next_active);
    } else {
      // The tab to the right moved into `index`; if the last tab closed, take
      // the new last tab.
      active_index_ = std::min(index, tabs_.size() - 1);
    }
  } else if (active_index_ && *active_index_ > index) {
    --*active_index_;
  }
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
  ActivateIndex((*active_index_ + 1) % tabs_.size());
}

void TabStripModel::ActivatePrevious() {
  if (tabs_.empty() || !active_index_) {
    return;
  }
  ActivateIndex((*active_index_ + tabs_.size() - 1) % tabs_.size());
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
  to_index = std::min(to_index, tabs_.size() - 1);
  if (to_index == *from) {
    return true;
  }
  std::optional<TabId> active = active_id();
  Entry entry = tabs_[*from];
  // Moving a tab is a user action that breaks Chrome's opener relation.
  entry.opener.reset();
  tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(*from));
  tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(to_index), entry);
  if (active) {
    active_index_ = IndexOf(*active);
  }
  return true;
}

std::optional<std::size_t> TabStripModel::IndexOf(TabId id) const {
  auto it =
      std::ranges::find_if(tabs_, [id](const Entry& e) { return e.id == id; });
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

void TabStripModel::ForgetOpeners() {
  for (Entry& entry : tabs_) {
    entry.opener.reset();
  }
}

}  // namespace brouk
