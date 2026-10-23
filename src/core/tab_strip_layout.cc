#include "core/tab_strip_layout.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace brouk {

namespace {

struct TabWidths {
  double normal = 0;
  double pinned = 0;
};

// Widths for `count` tabs, `pinned` of them pinned, sharing `space`.
TabWidths ShareSpace(double space,
                     std::size_t count,
                     std::size_t pinned,
                     const TabStripMetrics& m) {
  auto slot = [&m](double room, std::size_t tabs) {
    return std::floor(room / static_cast<double>(tabs) - m.tab_gap);
  };
  double even = std::clamp(slot(space, count), m.min_tab_width,
                           m.max_tab_width);
  if (pinned == 0 || pinned == count || even >= m.pinned_min_width) {
    return {even, even};
  }
  // Pinned tabs keep their minimum while the others can still shrink.
  double others = slot(space - static_cast<double>(pinned) *
                                   (m.pinned_min_width + m.tab_gap),
                       count - pinned);
  if (others >= m.min_tab_width) {
    return {others, m.pinned_min_width};
  }
  double shrunk_pinned = slot(space - static_cast<double>(count - pinned) *
                                          (m.min_tab_width + m.tab_gap),
                              pinned);
  return {m.min_tab_width, std::clamp(shrunk_pinned, m.min_tab_width,
                                      m.pinned_min_width)};
}

bool Fits(double space, std::size_t count, const TabStripMetrics& m) {
  return static_cast<double>(count) * (m.min_tab_width + m.tab_gap) <= space;
}

}  // namespace

std::vector<TabStripGroupLayout> LayoutTabStrip(
    const TabStripLayoutInput& input) {
  const TabStripMetrics& m = input.metrics;
  const std::vector<TabStripLayoutGroup>& groups = input.groups;
  std::vector<TabStripGroupLayout> result(groups.size());
  double space = input.width;
  for (std::size_t i = 0; i < groups.size(); ++i) {
    space -= groups[i].fixed_width;
    result[i].collapsed = groups[i].collapsed;
    result[i].tab_widths.assign(groups[i].tab_count, 0);
  }
  auto shown_tabs = [&] {
    std::size_t count = 0;
    for (std::size_t i = 0; i < groups.size(); ++i) {
      if (!result[i].collapsed) {
        count += groups[i].tab_count;
      }
    }
    return count;
  };

  std::optional<std::size_t> active;
  if (input.active_group && *input.active_group < groups.size() &&
      !groups[*input.active_group].collapsed &&
      groups[*input.active_group].tab_count > 0) {
    active = input.active_group;
  }

  // Step 2: collapse other groups, the biggest first.
  if (active && !Fits(space, shown_tabs(), m)) {
    std::vector<std::size_t> others;
    for (std::size_t i = 0; i < groups.size(); ++i) {
      if (i != *active && !result[i].collapsed && groups[i].tab_count > 0) {
        others.push_back(i);
      }
    }
    std::ranges::stable_sort(others, [&groups](std::size_t a, std::size_t b) {
      return groups[a].tab_count > groups[b].tab_count;
    });
    for (std::size_t i : others) {
      if (Fits(space, shown_tabs(), m)) {
        break;
      }
      result[i].collapsed = true;
      result[i].auto_collapsed = true;
    }
  }

  // Step 3: the active group alone does not fit; show a part of it.
  if (active && !Fits(space, shown_tabs(), m)) {
    const TabStripLayoutGroup& group = groups[*active];
    TabStripGroupLayout& layout = result[*active];
    space -= m.more_button_width;
    std::size_t pinned = std::min(group.pinned_count, group.tab_count);
    std::size_t current = std::min(input.active_tab, group.tab_count - 1);
    auto slots = static_cast<std::size_t>(
        std::max(std::floor(space / (m.min_tab_width + m.tab_gap)), 0.0));
    std::size_t window = slots > pinned ? slots - pinned : 0;
    if (current >= pinned) {
      window = std::max<std::size_t>(window, 1);  // The active tab is shown.
    }
    window = std::min(window, group.tab_count - pinned);
    std::size_t start = std::max(current, pinned);
    start = std::min(start, group.tab_count - window);
    TabWidths widths = ShareSpace(space, pinned + window, pinned, m);
    for (std::size_t i = 0; i < pinned; ++i) {
      layout.tab_widths[i] = widths.pinned;
    }
    for (std::size_t i = start; i < start + window; ++i) {
      layout.tab_widths[i] = widths.normal;
    }
    layout.hidden_count = group.tab_count - pinned - window;
    return result;
  }

  // Step 1: all shown tabs share the space.
  std::size_t count = 0;
  std::size_t pinned = 0;
  for (std::size_t i = 0; i < groups.size(); ++i) {
    if (!result[i].collapsed) {
      count += groups[i].tab_count;
      pinned += std::min(groups[i].pinned_count, groups[i].tab_count);
    }
  }
  if (count == 0) {
    return result;
  }
  TabWidths widths = ShareSpace(space, count, pinned, m);
  for (std::size_t i = 0; i < groups.size(); ++i) {
    if (result[i].collapsed) {
      continue;
    }
    for (std::size_t t = 0; t < groups[i].tab_count; ++t) {
      result[i].tab_widths[t] =
          t < groups[i].pinned_count ? widths.pinned : widths.normal;
    }
  }
  return result;
}

}  // namespace brouk
