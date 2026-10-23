#ifndef BROUK_CORE_TAB_STRIP_LAYOUT_H_
#define BROUK_CORE_TAB_STRIP_LAYOUT_H_

#include <cstddef>
#include <optional>
#include <vector>

namespace brouk {

// Sizes in points, chosen by the platform UI.
struct TabStripMetrics {
  double max_tab_width = 220;
  // Icon and close button only (the close button is always visible, 003).
  double min_tab_width = 44;
  // A pinned tab keeps the icon and the start of its title while space
  // allows.
  double pinned_min_width = 96;
  double tab_gap = 1;
  // The "N more" button of a group that shows only some of its tabs.
  double more_button_width = 40;
};

// One profile group as the layout needs it.
struct TabStripLayoutGroup {
  // Label, "+" button and gaps; taken whether the group is collapsed or not.
  double fixed_width = 0;
  std::size_t tab_count = 0;
  std::size_t pinned_count = 0;  // Its first `pinned_count` tabs.
  bool collapsed = false;        // By the user.
};

struct TabStripLayoutInput {
  std::vector<TabStripLayoutGroup> groups;
  // The group and position (inside it) of the active tab.
  std::optional<std::size_t> active_group;
  std::size_t active_tab = 0;
  // Width available for all groups.
  double width = 0;
  TabStripMetrics metrics;
};

// How one group is shown.
struct TabStripGroupLayout {
  // Shown collapsed: by the user, or by the layout for lack of space.
  bool collapsed = false;
  bool auto_collapsed = false;
  // One width per tab; 0 for tabs that are not shown.
  std::vector<double> tab_widths;
  // Tabs of an expanded group that are not shown (the "N more" button).
  std::size_t hidden_count = 0;
};

// Lays out the tab strip when tabs do not fit (003):
//  1. Tabs shrink, down to just their icon; pinned tabs stay wider while
//     space allows.
//  2. Other groups collapse, those with the most tabs first, to make room
//     for the active tab's group.
//  3. If that group alone still does not fit, it shows its pinned tabs, the
//     active tab and as many tabs after it as fit (at the end of the group,
//     the tabs before it fill the space), and hides the rest.
// Pure function of its input, so the rules are tested without any UI.
std::vector<TabStripGroupLayout> LayoutTabStrip(
    const TabStripLayoutInput& input);

}  // namespace brouk

#endif  // BROUK_CORE_TAB_STRIP_LAYOUT_H_
