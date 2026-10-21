#include "core/tab_strip_model.h"

#include <vector>

#include <gtest/gtest.h>

namespace brouk {
namespace {

std::vector<TabId> Order(const TabStripModel& model) {
  std::vector<TabId> ids;
  for (std::size_t i = 0; i < model.count(); ++i) {
    ids.push_back(model.id_at(i));
  }
  return ids;
}

TEST(TabStripModelTest, AppendActivates) {
  TabStripModel model;
  EXPECT_FALSE(model.active_id());
  model.AppendTab(1, true);
  model.AppendTab(2, true);
  model.AppendTab(3, false);
  EXPECT_EQ(Order(model), (std::vector<TabId>{1, 2, 3}));
  EXPECT_EQ(model.active_id(), 2);
}

TEST(TabStripModelTest, FirstTabIsActiveEvenInBackground) {
  TabStripModel model;
  model.AppendTab(1, false);
  EXPECT_EQ(model.active_id(), 1);
}

TEST(TabStripModelTest, OpenedTabsGoAfterOpenerInOrder) {
  TabStripModel model;
  model.AppendTab(1, true);
  model.AppendTab(2, false);
  model.InsertFromOpener(10, 1, false);
  model.InsertFromOpener(11, 1, false);
  EXPECT_EQ(Order(model), (std::vector<TabId>{1, 10, 11, 2}));
  EXPECT_EQ(model.active_id(), 1);
  EXPECT_EQ(model.OpenerOf(10), 1);
}

TEST(TabStripModelTest, InsertBeforeActiveKeepsActiveTab) {
  TabStripModel model;
  model.AppendTab(1, false);
  model.AppendTab(2, true);
  model.InsertFromOpener(10, 1, false);
  EXPECT_EQ(Order(model), (std::vector<TabId>{1, 10, 2}));
  EXPECT_EQ(model.active_id(), 2);
}

TEST(TabStripModelTest, UnknownOpenerAppends) {
  TabStripModel model;
  model.AppendTab(1, true);
  model.InsertFromOpener(2, 99, true);
  EXPECT_EQ(Order(model), (std::vector<TabId>{1, 2}));
  EXPECT_EQ(model.active_id(), 2);
}

TEST(TabStripModelTest, ClosingOpenedTabReturnsToOpener) {
  TabStripModel model;
  model.AppendTab(1, true);
  model.AppendTab(2, false);
  model.InsertFromOpener(10, 1, true);
  ASSERT_EQ(model.active_id(), 10);
  model.RemoveTab(10);
  EXPECT_EQ(model.active_id(), 1);
}

TEST(TabStripModelTest, ClosingOpenedTabPrefersSibling) {
  TabStripModel model;
  model.AppendTab(1, true);
  model.InsertFromOpener(10, 1, false);
  model.InsertFromOpener(11, 1, true);
  ASSERT_EQ(Order(model), (std::vector<TabId>{1, 10, 11}));
  model.RemoveTab(11);
  EXPECT_EQ(model.active_id(), 10);
}

TEST(TabStripModelTest, ClosingActiveTabSelectsRightThenLeft) {
  TabStripModel model;
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
  TabStripModel model;
  model.AppendTab(1, false);
  model.AppendTab(2, false);
  model.AppendTab(3, true);
  model.RemoveTab(1);
  EXPECT_EQ(model.active_id(), 3);
  EXPECT_EQ(model.active_index(), 1u);
  EXPECT_FALSE(model.RemoveTab(42));
}

TEST(TabStripModelTest, UserActivationForgetsOpeners) {
  TabStripModel model;
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
  TabStripModel model;
  model.AppendTab(1, true);
  model.InsertFromOpener(10, 1, false);
  model.RemoveTab(1);
  EXPECT_FALSE(model.OpenerOf(10).has_value());
  EXPECT_EQ(model.active_id(), 10);
}

TEST(TabStripModelTest, NextPreviousWrap) {
  TabStripModel model;
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
  TabStripModel model;
  for (TabId id = 1; id <= 10; ++id) {
    model.AppendTab(id, false);
  }
  model.ActivateByShortcutNumber(3);
  EXPECT_EQ(model.active_id(), 3);
  model.ActivateByShortcutNumber(9);
  EXPECT_EQ(model.active_id(), 10);
  model.ActivateByShortcutNumber(0);
  EXPECT_EQ(model.active_id(), 10);

  TabStripModel small;
  small.AppendTab(1, true);
  small.AppendTab(2, false);
  small.ActivateByShortcutNumber(5);
  EXPECT_EQ(small.active_id(), 1);
}

TEST(TabStripModelTest, MoveKeepsActiveTab) {
  TabStripModel model;
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

}  // namespace
}  // namespace brouk
