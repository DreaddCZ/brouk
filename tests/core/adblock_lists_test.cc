#include "core/adblock_lists.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace brouk {
namespace {

std::vector<std::string> Ids(const std::vector<FilterListDef>& lists) {
  std::vector<std::string> ids;
  for (const FilterListDef& list : lists) {
    ids.push_back(list.id);
  }
  return ids;
}

bool Has(const std::vector<std::string>& ids, const std::string& id) {
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}

TEST(AdblockListsTest, DefaultsFollowTheSpecTable) {
  std::vector<std::string> active = Ids(ActiveFilterLists({}, "en-US"));
  for (const char* on :
       {"easylist", "ublock-filters", "ublock-quick-fixes", "ublock-unbreak",
        "easyprivacy", "ublock-privacy", "url-tracking", "ublock-badware",
        "urlhaus", "easylist-cookie", "ublock-cookies"}) {
    EXPECT_TRUE(Has(active, on)) << on;
  }
  for (const char* off : {"hagezi-multi", "oisd-small", "stevenblack",
                          "fanboy-annoyance", "easylist-czsk"}) {
    EXPECT_FALSE(Has(active, off)) << off;
  }
  EXPECT_TRUE(Has(Ids(ActiveFilterLists({}, "cs-CZ")), "easylist-czsk"));
  EXPECT_TRUE(Has(Ids(ActiveFilterLists({}, "sk")), "easylist-czsk"));
}

TEST(AdblockListsTest, UserChangesAndCustomLists) {
  FilterListChoice choice;
  choice.turned_on = {"oisd-small"};
  choice.turned_off = {"easylist", "url-0000"};
  choice.custom = ParseCustomFilterLists(
      "ads=https://example.com/a.txt\n"
      "bogus=https://example.com/b.txt  privacy=ftp://x/c.txt "
      "ads=https://example.com/a.txt custom=https://example.org/d.txt");
  ASSERT_EQ(choice.custom.size(), 2U);
  EXPECT_EQ(choice.custom[0].category, FilterCategory::kAds);
  EXPECT_EQ(choice.custom[1].category, FilterCategory::kCustom);
  EXPECT_EQ(choice.custom[0].id,
            CustomFilterListId("https://example.com/a.txt"));
  EXPECT_NE(choice.custom[0].id, choice.custom[1].id);
  std::vector<std::string> active = Ids(ActiveFilterLists(choice, "en"));
  EXPECT_TRUE(Has(active, "oisd-small"));
  EXPECT_FALSE(Has(active, "easylist"));
  EXPECT_TRUE(Has(active, choice.custom[1].id));
  EXPECT_EQ(FormatCustomFilterLists(choice.custom),
            "ads=https://example.com/a.txt custom=https://example.org/d.txt");
}

TEST(AdblockListsTest, ReadsExpiresHeaders) {
  EXPECT_EQ(ParseExpiresHours("[Adblock Plus 2.0]\n! Title: EasyList\n"
                              "! Expires: 4 days (update frequency)\n||a^\n"),
            96);
  EXPECT_EQ(ParseExpiresHours("! Expires: 8 hours\n"), 8);
  EXPECT_EQ(ParseExpiresHours("! Expires: 1 hours\n"), 1);
  EXPECT_EQ(ParseExpiresHours("# Expires: 2\n0.0.0.0 a.example\n"), 48);
  EXPECT_EQ(ParseExpiresHours("||a^\n! Expires: 1 day\n"), std::nullopt)
      << "only the header counts";
  EXPECT_EQ(ParseExpiresHours("! Expires: soon\n"), std::nullopt);
  EXPECT_EQ(ParseExpiresHours(""), std::nullopt);
}

TEST(AdblockListsTest, UpdateIntervals) {
  EXPECT_EQ(UpdateIntervalHours(std::nullopt), 96);
  EXPECT_EQ(UpdateIntervalHours(1), 6) << "at most every 6 hours";
  EXPECT_EQ(UpdateIntervalHours(120), 120);
}

TEST(AdblockListsTest, Categories) {
  EXPECT_EQ(DefaultFilterCategories().count(FilterCategory::kDomains), 0U);
  EXPECT_EQ(DefaultFilterCategories().count(FilterCategory::kAnnoyances), 0U);
  EXPECT_EQ(DefaultFilterCategories().count(FilterCategory::kCookies), 1U);
  std::set<FilterCategory> parsed =
      ParseFilterCategories("privacy, ads,nonsense ,cookies");
  EXPECT_EQ(parsed, (std::set<FilterCategory>{FilterCategory::kAds,
                                              FilterCategory::kPrivacy,
                                              FilterCategory::kCookies}));
  EXPECT_EQ(FormatFilterCategories(parsed), "ads, privacy, cookies");
  for (FilterCategory category : AllFilterCategories()) {
    EXPECT_EQ(FilterCategoryFromId(FilterCategoryId(category)), category);
  }
}

TEST(AdblockListsTest, BuiltinIdsAreUnique) {
  std::set<std::string> ids;
  for (const FilterListDef& list : BuiltinFilterLists()) {
    EXPECT_TRUE(ids.insert(list.id).second) << list.id;
    EXPECT_TRUE(list.url.starts_with("https://")) << list.id;
    EXPECT_EQ(FindBuiltinFilterList(list.id), &list);
  }
  EXPECT_EQ(FindBuiltinFilterList("nope"), nullptr);
}

}  // namespace
}  // namespace brouk
