#include "core/tab_strip_layout.h"

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

namespace brouk {
namespace {

// With the default metrics a tab needs at least 45 points (44 + gap).
TabStripLayoutGroup Group(std::size_t tabs,
                          std::size_t pinned = 0,
                          bool collapsed = false) {
  return {.fixed_width = 100,
          .tab_count = tabs,
          .pinned_count = pinned,
          .collapsed = collapsed};
}

std::size_t Shown(const TabStripGroupLayout& layout) {
  std::size_t count = 0;
  for (double width : layout.tab_widths) {
    count += width > 0 ? 1 : 0;
  }
  return count;
}

TEST(TabStripLayoutTest, TabsShareTheSpaceUpToTheirMaximum) {
  TabStripLayoutInput input{
      .groups = {Group(2), Group(1)}, .active_group = 1, .width = 5000};
  std::vector<TabStripGroupLayout> layout = LayoutTabStrip(input);
  EXPECT_EQ(layout[0].tab_widths, (std::vector<double>{220, 220}));
  EXPECT_EQ(layout[1].tab_widths, (std::vector<double>{220}));

  input.width = 200 + 3 * 101;  // 101 points per tab.
  layout = LayoutTabStrip(input);
  EXPECT_EQ(layout[0].tab_widths, (std::vector<double>{100, 100}));
  EXPECT_FALSE(layout[0].collapsed);
}

TEST(TabStripLayoutTest, UserCollapsedGroupsShowNoTabs) {
  TabStripLayoutInput input{.groups = {Group(2, 0, true), Group(1)},
                            .active_group = 1,
                            .width = 5000};
  std::vector<TabStripGroupLayout> layout = LayoutTabStrip(input);
  EXPECT_TRUE(layout[0].collapsed);
  EXPECT_FALSE(layout[0].auto_collapsed);
  EXPECT_EQ(Shown(layout[0]), 0U);
}

TEST(TabStripLayoutTest, PinnedTabsStayWiderWhileSpaceAllows) {
  // 4 tabs, 1 pinned, 60 points each if shared evenly.
  TabStripLayoutInput input{
      .groups = {Group(4, 1)}, .active_group = 0, .width = 100 + 4 * 61};
  std::vector<TabStripGroupLayout> layout = LayoutTabStrip(input);
  EXPECT_EQ(layout[0].tab_widths[0], 96);
  // (244 - 97) / 3 - 1 = 48.
  EXPECT_EQ(layout[0].tab_widths[1], 48);

  // No room for the wider pinned tab: everything at the minimum.
  input.width = 100 + 4 * 45;
  layout = LayoutTabStrip(input);
  EXPECT_EQ(layout[0].tab_widths, (std::vector<double>{44, 44, 44, 44}));

  // Some room: the pinned tab gets what is left above the others' minimum.
  input.width = 100 + 4 * 45 + 20;
  layout = LayoutTabStrip(input);
  EXPECT_EQ(layout[0].tab_widths[0], 64);
  EXPECT_EQ(layout[0].tab_widths[1], 44);
}

TEST(TabStripLayoutTest, OtherGroupsCollapseBiggestFirst) {
  // Groups of 3, 6 and 2 tabs; the 2-tab group is active.
  TabStripLayoutInput input{.groups = {Group(3), Group(6), Group(2)},
                            .active_group = 2,
                            .active_tab = 1,
                            .width = 300 + 6 * 45};
  std::vector<TabStripGroupLayout> layout = LayoutTabStrip(input);
  EXPECT_FALSE(layout[0].collapsed);
  EXPECT_TRUE(layout[1].collapsed);
  EXPECT_TRUE(layout[1].auto_collapsed);
  EXPECT_FALSE(layout[2].collapsed);
  EXPECT_EQ(Shown(layout[0]), 3U);
  EXPECT_EQ(Shown(layout[2]), 2U);

  // Less space: both others collapse.
  input.width = 300 + 3 * 45;
  layout = LayoutTabStrip(input);
  EXPECT_TRUE(layout[0].auto_collapsed);
  EXPECT_TRUE(layout[1].auto_collapsed);
  EXPECT_EQ(layout[2].hidden_count, 0U);
}

TEST(TabStripLayoutTest, OneGroupTooBigShowsTheActiveTabAndTheNextOnes) {
  // 20 tabs, 2 pinned, active is tab 5; room for 6 tabs after the
  // "N more" button.
  TabStripLayoutInput input{.groups = {Group(3), Group(20, 2)},
                            .active_group = 1,
                            .active_tab = 5,
                            .width = 200 + 40 + 6 * 45};
  std::vector<TabStripGroupLayout> layout = LayoutTabStrip(input);
  EXPECT_TRUE(layout[0].auto_collapsed);
  const std::vector<double>& widths = layout[1].tab_widths;
  EXPECT_GT(widths[0], 0);
  EXPECT_GT(widths[1], 0);
  EXPECT_EQ(widths[2], 0);
  EXPECT_EQ(widths[4], 0);
  for (std::size_t i = 5; i < 9; ++i) {
    EXPECT_GT(widths[i], 0) << i;
  }
  EXPECT_EQ(widths[9], 0);
  EXPECT_EQ(layout[1].hidden_count, 14U);
}

TEST(TabStripLayoutTest, AtTheEndTheTabsBeforeFillTheSpace) {
  TabStripLayoutInput input{.groups = {Group(10)},
                            .active_group = 0,
                            .active_tab = 9,
                            .width = 100 + 40 + 4 * 45};
  std::vector<TabStripGroupLayout> layout = LayoutTabStrip(input);
  const std::vector<double>& widths = layout[0].tab_widths;
  EXPECT_EQ(widths[5], 0);
  for (std::size_t i = 6; i < 10; ++i) {
    EXPECT_GT(widths[i], 0) << i;
  }
  EXPECT_EQ(layout[0].hidden_count, 6U);
}

TEST(TabStripLayoutTest, ActiveTabIsShownEvenWithoutRoom) {
  TabStripLayoutInput input{.groups = {Group(10, 3)},
                            .active_group = 0,
                            .active_tab = 7,
                            .width = 100};
  std::vector<TabStripGroupLayout> layout = LayoutTabStrip(input);
  EXPECT_GT(layout[0].tab_widths[7], 0);
  EXPECT_GT(layout[0].tab_widths[0], 0);  // Pinned tabs too.
  EXPECT_EQ(Shown(layout[0]), 4U);
  EXPECT_EQ(layout[0].hidden_count, 6U);

  // An active pinned tab: only the pinned tabs.
  input.active_tab = 1;
  layout = LayoutTabStrip(input);
  EXPECT_EQ(Shown(layout[0]), 3U);
}

TEST(TabStripLayoutTest, EmptyStrip) {
  TabStripLayoutInput input{.groups = {Group(0)}, .width = 500};
  std::vector<TabStripGroupLayout> layout = LayoutTabStrip(input);
  ASSERT_EQ(layout.size(), 1U);
  EXPECT_TRUE(layout[0].tab_widths.empty());
}

}  // namespace
}  // namespace brouk
