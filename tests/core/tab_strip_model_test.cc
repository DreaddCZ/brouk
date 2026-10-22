#include "core/tab_strip_model.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace brouk {
namespace {

const ProfileId kMain = "default";
const ProfileId kWork = "p-work";
const ProfileId kHome = "p-home";

std::vector<TabId> Order(const TabStripModel& model) {
  std::vector<TabId> ids;
  for (std::size_t i = 0; i < model.count(); ++i) {
    ids.push_back(model.id_at(i));
  }
  return ids;
}

TEST(TabStripModelTest, AppendActivates) {
  TabStripModel model(kMain);
  EXPECT_FALSE(model.active_id());
  model.AppendTab(1, true);
  model.AppendTab(2, true);
  model.AppendTab(3, false);
  EXPECT_EQ(Order(model), (std::vector<TabId>{1, 2, 3}));
  EXPECT_EQ(model.active_id(), 2);
}

TEST(TabStripModelTest, FirstTabIsActiveEvenInBackground) {
  TabStripModel model(kMain);
  model.AppendTab(1, false);
  EXPECT_EQ(model.active_id(), 1);
}

TEST(TabStripModelTest, OpenedTabsGoAfterOpenerInOrder) {
  TabStripModel model(kMain);
  model.AppendTab(1, true);
  model.AppendTab(2, false);
  model.InsertFromOpener(10, 1, false);
  model.InsertFromOpener(11, 1, false);
  EXPECT_EQ(Order(model), (std::vector<TabId>{1, 10, 11, 2}));
  EXPECT_EQ(model.active_id(), 1);
  EXPECT_EQ(model.OpenerOf(10), 1);
}

TEST(TabStripModelTest, InsertBeforeActiveKeepsActiveTab) {
  TabStripModel model(kMain);
  model.AppendTab(1, false);
  model.AppendTab(2, true);
  model.InsertFromOpener(10, 1, false);
  EXPECT_EQ(Order(model), (std::vector<TabId>{1, 10, 2}));
  EXPECT_EQ(model.active_id(), 2);
}

TEST(TabStripModelTest, UnknownOpenerAppends) {
  TabStripModel model(kMain);
  model.AppendTab(1, true);
  model.InsertFromOpener(2, 99, true);
  EXPECT_EQ(Order(model), (std::vector<TabId>{1, 2}));
  EXPECT_EQ(model.active_id(), 2);
}

TEST(TabStripModelTest, ClosingOpenedTabReturnsToOpener) {
  TabStripModel model(kMain);
  model.AppendTab(1, true);
  model.AppendTab(2, false);
  model.InsertFromOpener(10, 1, true);
  ASSERT_EQ(model.active_id(), 10);
  model.RemoveTab(10);
  EXPECT_EQ(model.active_id(), 1);
}

TEST(TabStripModelTest, ClosingOpenedTabPrefersSibling) {
  TabStripModel model(kMain);
  model.AppendTab(1, true);
  model.InsertFromOpener(10, 1, false);
  model.InsertFromOpener(11, 1, true);
  ASSERT_EQ(Order(model), (std::vector<TabId>{1, 10, 11}));
  model.RemoveTab(11);
  EXPECT_EQ(model.active_id(), 10);
}

TEST(TabStripModelTest, ClosingActiveTabSelectsRightThenLeft) {
  TabStripModel model(kMain);
  model.AppendTab(1, false);
  model.AppendTab(2, true);
  model.AppendTab(3, false);
  model.RemoveTab(2);
  EXPECT_EQ(model.active_id(), 3);
  model.RemoveTab(3);
  EXPECT_EQ(model.active_id(), 1);
  model.RemoveTab(1);
  EXPECT_FALSE(model.active_id());
  EXPECT_TRUE(model.empty());
}

TEST(TabStripModelTest, ClosingInactiveTabKeepsActive) {
  TabStripModel model(kMain);
  model.AppendTab(1, false);
  model.AppendTab(2, false);
  model.AppendTab(3, true);
  model.RemoveTab(1);
  EXPECT_EQ(model.active_id(), 3);
  EXPECT_EQ(model.active_index(), 1u);
  EXPECT_FALSE(model.RemoveTab(42));
}

TEST(TabStripModelTest, UserActivationForgetsOpeners) {
  TabStripModel model(kMain);
  model.AppendTab(1, true);
  model.AppendTab(2, false);
  model.InsertFromOpener(10, 1, true);
  model.ActivateTab(2);
  model.ActivateTab(10);
  model.RemoveTab(10);
  // Opener forgotten: falls back to the tab on the right.
  EXPECT_EQ(model.active_id(), 2);
}

TEST(TabStripModelTest, ClosingOpenerClearsChildren) {
  TabStripModel model(kMain);
  model.AppendTab(1, true);
  model.InsertFromOpener(10, 1, false);
  model.RemoveTab(1);
  EXPECT_FALSE(model.OpenerOf(10).has_value());
  EXPECT_EQ(model.active_id(), 10);
}

TEST(TabStripModelTest, NextPreviousWrap) {
  TabStripModel model(kMain);
  model.AppendTab(1, true);
  model.AppendTab(2, false);
  model.AppendTab(3, false);
  model.ActivatePrevious();
  EXPECT_EQ(model.active_id(), 3);
  model.ActivateNext();
  EXPECT_EQ(model.active_id(), 1);
  model.ActivateNext();
  EXPECT_EQ(model.active_id(), 2);
}

TEST(TabStripModelTest, ShortcutNumbers) {
  TabStripModel model(kMain);
  for (TabId id = 1; id <= 10; ++id) {
    model.AppendTab(id, false);
  }
  model.ActivateByShortcutNumber(3);
  EXPECT_EQ(model.active_id(), 3);
  model.ActivateByShortcutNumber(9);
  EXPECT_EQ(model.active_id(), 10);
  model.ActivateByShortcutNumber(0);
  EXPECT_EQ(model.active_id(), 10);

  TabStripModel small(kMain);
  small.AppendTab(1, true);
  small.AppendTab(2, false);
  small.ActivateByShortcutNumber(5);
  EXPECT_EQ(small.active_id(), 1);
}

TEST(TabStripModelTest, MoveKeepsActiveTab) {
  TabStripModel model(kMain);
  model.AppendTab(1, true);
  model.AppendTab(2, false);
  model.AppendTab(3, false);
  model.MoveTab(1, 2);
  EXPECT_EQ(Order(model), (std::vector<TabId>{2, 3, 1}));
  EXPECT_EQ(model.active_id(), 1);
  model.MoveTab(3, 100);
  EXPECT_EQ(Order(model), (std::vector<TabId>{2, 1, 3}));
  EXPECT_EQ(model.active_id(), 1);
  EXPECT_FALSE(model.MoveTab(42, 0));
}

// Profile groups (003, 005).

std::string Groups(const TabStripModel& model) {
  std::string text;
  for (const TabStripModel::Group& g : model.groups()) {
    text += g.profile + ":" + std::to_string(g.start) + "+" +
            std::to_string(g.count) + (g.collapsed ? "c" : "") + " ";
  }
  return text;
}

TEST(TabStripModelGroupsTest, DefaultGroupAlwaysExistsAndIsLast) {
  TabStripModel model(kMain);
  EXPECT_EQ(Groups(model), "default:0+0 ");
  model.AppendTab(1, kWork, true);
  EXPECT_EQ(Groups(model), "p-work:0+1 default:1+0 ");
  model.AppendTab(2, true);
  model.AppendTab(3, kHome, true);
  model.AppendTab(4, kWork, true);
  EXPECT_EQ(Order(model), (std::vector<TabId>{1, 4, 3, 2}));
  EXPECT_EQ(Groups(model), "p-work:0+2 p-home:2+1 default:3+1 ");
  EXPECT_EQ(*model.ProfileOf(3), kHome);
}

TEST(TabStripModelGroupsTest, GroupDisappearsWithItsLastTab) {
  TabStripModel model(kMain);
  model.AppendTab(1, kWork, true);
  model.AppendTab(2, true);
  model.RemoveTab(1);
  EXPECT_EQ(Groups(model), "default:0+1 ");
  model.RemoveTab(2);
  EXPECT_EQ(Groups(model), "default:0+0 ");
  // A group that comes back is added again (before the default group).
  model.AppendTab(3, true);
  model.AppendTab(4, kWork, true);
  EXPECT_EQ(Order(model), (std::vector<TabId>{4, 3}));
}

TEST(TabStripModelGroupsTest, OpenedTabsStayInTheOpenersProfile) {
  TabStripModel model(kMain);
  model.AppendTab(1, kWork, true);
  model.AppendTab(2, true);
  model.InsertFromOpener(10, 1, false);
  EXPECT_EQ(Order(model), (std::vector<TabId>{1, 10, 2}));
  EXPECT_EQ(*model.ProfileOf(10), kWork);
  EXPECT_EQ(Groups(model), "p-work:0+2 default:2+1 ");
}

TEST(TabStripModelGroupsTest, MovesStayInsideTheGroup) {
  TabStripModel model(kMain);
  model.AppendTab(1, kWork, true);
  model.AppendTab(2, kWork, true);
  model.AppendTab(3, true);
  model.AppendTab(4, true);
  model.MoveTab(4, 0);
  EXPECT_EQ(Order(model), (std::vector<TabId>{1, 2, 4, 3}));
  model.MoveTab(1, 99);
  EXPECT_EQ(Order(model), (std::vector<TabId>{2, 1, 4, 3}));
}

TEST(TabStripModelGroupsTest, CollapsingTheActiveGroupActivatesAnotherTab) {
  TabStripModel model(kMain);
  model.AppendTab(1, kWork, true);
  model.AppendTab(2, kWork, true);
  model.AppendTab(3, true);
  EXPECT_TRUE(model.SetGroupCollapsed(kMain, true));
  EXPECT_EQ(model.active_id(), 2);  // The nearest one, to the left.
  EXPECT_EQ(Groups(model), "p-work:0+2 default:2+1c ");
  // Nothing outside the work group is visible now: it stays expanded.
  EXPECT_FALSE(model.SetGroupCollapsed(kWork, true));
  // Activating a tab expands its group.
  model.ActivateTab(3);
  EXPECT_EQ(Groups(model), "p-work:0+2 default:2+1 ");
  EXPECT_TRUE(model.SetGroupCollapsed(kWork, true));
  EXPECT_EQ(model.active_id(), 3);
  EXPECT_FALSE(model.SetGroupCollapsed(kHome, true));  // No such group.
}

TEST(TabStripModelGroupsTest, SwitchingTabsSkipsCollapsedGroups) {
  TabStripModel model(kMain);
  model.AppendTab(1, kWork, true);
  model.AppendTab(2, kHome, true);
  model.AppendTab(3, true);
  model.SetGroupCollapsed(kHome, true);
  model.ActivateTab(1);
  model.ActivateNext();
  EXPECT_EQ(model.active_id(), 3);
  model.ActivateNext();
  EXPECT_EQ(model.active_id(), 1);
  model.ActivatePrevious();
  EXPECT_EQ(model.active_id(), 3);
}

TEST(TabStripModelGroupsTest, ClosingPrefersVisibleTabs) {
  TabStripModel model(kMain);
  model.AppendTab(1, kWork, true);
  model.AppendTab(2, kHome, true);
  model.AppendTab(3, kHome, true);
  model.AppendTab(4, true);
  model.SetGroupCollapsed(kHome, true);
  model.ActivateTab(1);
  model.RemoveTab(1);  // Right of it is the collapsed home group.
  EXPECT_EQ(model.active_id(), 4);
  EXPECT_EQ(Groups(model), "p-home:0+2c default:2+1 ");
  model.RemoveTab(4);  // Only collapsed tabs are left: one is shown.
  EXPECT_EQ(model.active_id(), 3);  // The nearest, to the left.
  EXPECT_EQ(Groups(model), "p-home:0+2 default:2+0 ");
}

TEST(TabStripModelGroupsTest, ChangingTheDefaultProfileMovesGroups) {
  TabStripModel model(kMain);
  model.AppendTab(1, kWork, true);
  model.AppendTab(2, kHome, true);
  model.AppendTab(3, true);
  model.SetDefaultProfile(kWork);
  EXPECT_EQ(Order(model), (std::vector<TabId>{2, 3, 1}));
  EXPECT_EQ(Groups(model), "p-home:0+1 default:1+1 p-work:2+1 ");
  EXPECT_EQ(model.active_id(), 3);
  model.AppendTab(4, false);  // New tabs go to the new default profile.
  EXPECT_EQ(*model.ProfileOf(4), kWork);
}

}  // namespace
}  // namespace brouk
