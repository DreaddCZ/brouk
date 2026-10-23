#include "core/closed_tabs.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace brouk {
namespace {

ClosedTab Tab(std::string url, ProfileId profile = "default") {
  return {.url = std::move(url),
          .title = "",
          .profile = std::move(profile),
          .pinned = false,
          .window_id = 1,
          .position = 0};
}

TEST(ClosedTabListTest, NewestFirstAndAtMostTen) {
  ClosedTabList list;
  for (int i = 0; i < 12; ++i) {
    list.AddTab(Tab("https://example.com/" + std::to_string(i)));
  }
  ASSERT_EQ(list.entries().size(), ClosedTabList::kMaxEntries);
  EXPECT_EQ(list.entries().front().tabs[0].url, "https://example.com/11");
  EXPECT_EQ(list.entries().back().tabs[0].url, "https://example.com/2");
  std::optional<ClosedEntry> newest = list.TakeNewest();
  ASSERT_TRUE(newest);
  EXPECT_EQ(newest->tabs[0].url, "https://example.com/11");
  EXPECT_EQ(list.entries().size(), 9U);
}

TEST(ClosedTabListTest, NewTabPagesAreNotKept) {
  ClosedTabList list;
  list.AddTab(Tab(""));
  list.AddTab(Tab("about:blank"));
  EXPECT_TRUE(list.empty());
  list.AddWindow(2, {Tab("about:blank"), Tab("")});
  EXPECT_TRUE(list.empty());
  list.AddWindow(2, {Tab("about:blank"), Tab("https://a.com/")});
  ASSERT_EQ(list.entries().size(), 1U);
  EXPECT_TRUE(list.entries()[0].is_window);
  EXPECT_EQ(list.entries()[0].tabs.size(), 1U);
}

TEST(ClosedTabListTest, TakeById) {
  ClosedTabList list;
  list.AddTab(Tab("https://a.com/"));
  list.AddTab(Tab("https://b.com/"));
  std::int64_t first = list.entries().back().id;
  std::optional<ClosedEntry> taken = list.Take(first);
  ASSERT_TRUE(taken);
  EXPECT_EQ(taken->tabs[0].url, "https://a.com/");
  EXPECT_FALSE(list.Take(first));
  EXPECT_FALSE(list.Take(999));
  list.TakeNewest();
  EXPECT_FALSE(list.TakeNewest());
}

TEST(ClosedTabListTest, DeletedProfileTabsAreRemoved) {
  ClosedTabList list;
  list.AddTab(Tab("https://work.com/", "p-work"));
  list.AddWindow(3, {Tab("https://a.com/"), Tab("https://w.com/", "p-work")});
  list.RemoveProfile("p-work");
  ASSERT_EQ(list.entries().size(), 1U);
  EXPECT_EQ(list.entries()[0].tabs.size(), 1U);
  EXPECT_EQ(list.entries()[0].tabs[0].url, "https://a.com/");
}

}  // namespace
}  // namespace brouk
