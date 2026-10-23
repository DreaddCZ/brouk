#include "core/history_query.h"

#include <ctime>
#include <string>

#include <gtest/gtest.h>

namespace brouk {
namespace {

// 2026-10-23 15:00 local time.
std::int64_t LocalTimeOf(int year, int month, int day, int hour) {
  std::tm local{};
  local.tm_year = year - 1900;
  local.tm_mon = month - 1;
  local.tm_mday = day;
  local.tm_hour = hour;
  local.tm_isdst = -1;
  return static_cast<std::int64_t>(std::mktime(&local));
}

const std::int64_t kNow = LocalTimeOf(2026, 10, 23, 15);

struct Item {
  std::string url = "https://companya.atlassian.net/browse/BR-1";
  std::string title = "Sprint board – Účetnictví";
  std::string host = "companya.atlassian.net";
  std::int64_t time = LocalTimeOf(2026, 10, 22, 10);
  VisitTransition transition = VisitTransition::kLink;
  std::int64_t page_visits = 4;
  std::string profile = "Work";

  QueryItem View() const {
    return {.url = url,
            .title = title,
            .host = host,
            .time = time,
            .transition = transition,
            .page_visits = page_visits,
            .profile_name = profile};
  }
};

bool Matches(std::string_view query, const Item& item = {}) {
  HistoryQuery parsed = HistoryQuery::Parse(query, kNow);
  EXPECT_TRUE(parsed.problems().empty()) << query;
  return parsed.Matches(item.View());
}

TEST(HistoryQueryTest, EmptyMatchesEverything) {
  HistoryQuery query = HistoryQuery::Parse("   ", kNow);
  EXPECT_TRUE(query.empty());
  EXPECT_TRUE(query.Matches(Item{}.View()));
}

TEST(HistoryQueryTest, WordsMatchTitleOrUrlAllRequired) {
  EXPECT_TRUE(Matches("sprint"));
  EXPECT_TRUE(Matches("browse br-1"));
  EXPECT_TRUE(Matches("UCETNICTVI"));  // Case and accents ignored.
  EXPECT_FALSE(Matches("sprint retro"));
}

TEST(HistoryQueryTest, PhrasesExclusionAndOr) {
  EXPECT_TRUE(Matches("\"sprint board\""));
  EXPECT_FALSE(Matches("\"board sprint\""));
  EXPECT_FALSE(Matches("-sprint"));
  EXPECT_TRUE(Matches("-retro"));
  EXPECT_TRUE(Matches("retro OR sprint"));
  EXPECT_FALSE(Matches("retro OR planning"));
  // OR binds closer than AND: board and (retro or sprint).
  EXPECT_TRUE(Matches("board retro OR sprint"));
  EXPECT_FALSE(Matches("retro retro OR sprint"));
}

TEST(HistoryQueryTest, SiteHostTitleUrl) {
  EXPECT_TRUE(Matches("site:atlassian.net"));
  EXPECT_TRUE(Matches("site:*.atlassian.net"));
  EXPECT_TRUE(Matches("site:companya.atlassian.net"));
  EXPECT_FALSE(Matches("site:lassian.net"));
  EXPECT_TRUE(Matches("host:companya.atlassian.net"));
  EXPECT_FALSE(Matches("host:atlassian.net"));
  EXPECT_FALSE(Matches("-site:atlassian.net"));
  EXPECT_TRUE(Matches("title:sprint"));
  EXPECT_FALSE(Matches("title:browse"));
  EXPECT_TRUE(Matches("url:/browse/"));
  EXPECT_TRUE(Matches("title:\"sprint board\""));
  // A URL is text, not a field.
  EXPECT_TRUE(Matches("https://companya"));
}

TEST(HistoryQueryTest, Dates) {
  EXPECT_TRUE(Matches("on:2026-10-22"));
  EXPECT_FALSE(Matches("on:2026-10-23"));
  EXPECT_TRUE(Matches("on:yesterday"));
  EXPECT_FALSE(Matches("on:today"));
  EXPECT_TRUE(Matches("after:2026-10-22"));
  EXPECT_FALSE(Matches("after:2026-10-23"));
  EXPECT_TRUE(Matches("before:2026-10-23"));
  EXPECT_FALSE(Matches("before:2026-10-22"));
  EXPECT_TRUE(Matches("after:7d"));
  EXPECT_FALSE(Matches("before:7d"));
  EXPECT_TRUE(Matches("after:1w"));
  Item old;
  old.time = LocalTimeOf(2026, 6, 1, 12);
  EXPECT_FALSE(Matches("after:3m", old));
  EXPECT_TRUE(Matches("after:1y", old));
  EXPECT_TRUE(Matches("after:2026-01-01 before:2026-07-01", old));
}

TEST(HistoryQueryTest, VisitsProfileType) {
  EXPECT_TRUE(Matches("visits:>3"));
  EXPECT_FALSE(Matches("visits:>4"));
  EXPECT_TRUE(Matches("visits:>=4"));
  EXPECT_TRUE(Matches("visits:4"));
  EXPECT_TRUE(Matches("visits:<5"));
  EXPECT_TRUE(Matches("visits:<=4"));
  EXPECT_TRUE(Matches("profile:work"));
  EXPECT_FALSE(Matches("profile:main"));
  EXPECT_TRUE(Matches("type:link"));
  EXPECT_FALSE(Matches("type:typed"));
  EXPECT_TRUE(Matches("type:typed OR type:link"));
}

TEST(HistoryQueryTest, InvalidFiltersAreReportedAndSkipped) {
  HistoryQuery query =
      HistoryQuery::Parse("sprint after:yesterdayy visits:many type:x", kNow);
  ASSERT_EQ(query.problems().size(), 3U);
  EXPECT_EQ(query.problems()[0].start, 7U);
  EXPECT_EQ(query.problems()[0].end, 23U);
  EXPECT_NE(query.problems()[0].message.find("not a date"), std::string::npos);
  EXPECT_TRUE(query.Matches(Item{}.View()));  // "sprint" still applies.
  EXPECT_EQ(HistoryQuery::Parse("OR sprint", kNow).problems().size(), 1U);
  EXPECT_EQ(HistoryQuery::Parse("sprint OR", kNow).problems().size(), 1U);
  EXPECT_EQ(HistoryQuery::Parse("site:", kNow).problems().size(), 1U);
  EXPECT_EQ(HistoryQuery::Parse("on:7d", kNow).problems().size(), 1U);
}

TEST(HistoryQueryTest, StartOfLocalDay) {
  EXPECT_EQ(StartOfLocalDay(kNow), LocalTimeOf(2026, 10, 23, 0));
}

}  // namespace
}  // namespace brouk
