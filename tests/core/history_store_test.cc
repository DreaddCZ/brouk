#include "core/history_store.h"

#include "core/history_query.h"

#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

namespace brouk {
namespace {

constexpr std::int64_t kDay = 24 * 60 * 60;
constexpr std::int64_t kNow = 1'800'000'000;

class HistoryStoreTest : public testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("brouk-history-test-" +
            std::string(
                testing::UnitTest::GetInstance()->current_test_info()->name()));
    std::error_code error;
    std::filesystem::remove_all(dir_, error);
    std::filesystem::create_directories(dir_);
    store_ = OpenStore();
  }
  void TearDown() override {
    store_.reset();
    std::error_code error;
    std::filesystem::remove_all(dir_, error);
  }
  std::unique_ptr<HistoryStore> OpenStore() {
    Result<std::unique_ptr<HistoryStore>> store =
        HistoryStore::Open(dir_ / "history.db");
    EXPECT_TRUE(store) << (store ? "" : store.error().message);
    return store ? std::move(store).value() : nullptr;
  }
  void Visit(const std::string& url,
             const std::string& title,
             std::int64_t time = kNow,
             VisitTransition transition = VisitTransition::kLink) {
    ASSERT_FALSE(store_->RecordVisit(url, title, time, transition));
  }
  std::vector<std::string> Urls(const std::string& input,
                                std::size_t limit = 10) {
    std::vector<std::string> urls;
    for (const HistoryMatch& match : store_->Suggest(input, kNow, limit)) {
      urls.push_back(match.url);
    }
    return urls;
  }

  std::filesystem::path dir_;
  std::unique_ptr<HistoryStore> store_;
};

TEST(HistoryRulesTest, OnlyWebAndFilePagesAreRecorded) {
  EXPECT_TRUE(ShouldRecordInHistory("https://example.com/"));
  EXPECT_TRUE(ShouldRecordInHistory("HTTP://example.com/"));
  EXPECT_TRUE(ShouldRecordInHistory("file:///Users/me/a.html"));
  EXPECT_FALSE(ShouldRecordInHistory("brouk://settings/"));
  EXPECT_FALSE(ShouldRecordInHistory("data:text/html,hi"));
  EXPECT_FALSE(ShouldRecordInHistory("blob:https://example.com/123"));
  EXPECT_FALSE(ShouldRecordInHistory("about:blank"));
  EXPECT_FALSE(ShouldRecordInHistory("chrome-error://chromewebdata/"));
}

TEST(HistoryRulesTest, UrlForMatching) {
  EXPECT_EQ(UrlForMatching("https://www.GitHub.com/DreaddCZ/Brouk"),
            "github.com/DreaddCZ/Brouk");
  EXPECT_EQ(UrlForMatching("http://example.com"), "example.com");
  EXPECT_EQ(UrlForMatching("file:///Users/Me"), "file:///users/me");
}

TEST_F(HistoryStoreTest, RecordsVisitsAndCountsThem) {
  Visit("https://example.com/", "", kNow - kDay, VisitTransition::kTyped);
  Visit("https://example.com/", "Example", kNow);
  std::optional<HistoryMatch> page = store_->FindPage("https://example.com/");
  ASSERT_TRUE(page);
  EXPECT_EQ(page->title, "Example");
  EXPECT_EQ(page->visit_count, 2);
  EXPECT_EQ(page->typed_count, 1);
  EXPECT_EQ(page->last_visit, kNow);
  // A later empty title keeps the known one; SetTitle changes it.
  Visit("https://example.com/", "", kNow);
  EXPECT_EQ(store_->FindPage("https://example.com/")->title, "Example");
  ASSERT_FALSE(store_->SetTitle("https://example.com/", "New"));
  EXPECT_EQ(store_->FindPage("https://example.com/")->title, "New");
}

TEST_F(HistoryStoreTest, SurvivesReopening) {
  Visit("https://example.com/", "Example");
  store_ = OpenStore();
  ASSERT_TRUE(store_);
  EXPECT_TRUE(store_->FindPage("https://example.com/"));
}

TEST_F(HistoryStoreTest, EveryWordMustMatchUrlOrTitle) {
  Visit("https://jira.example.com/browse/BR-1", "Sprint board – Brouk");
  Visit("https://github.com/brouk/brouk", "Brouk repository");
  Visit("https://news.example.org/", "News");
  EXPECT_EQ(Urls("brouk sprint"),
            (std::vector<std::string>{"https://jira.example.com/browse/BR-1"}));
  EXPECT_EQ(Urls("github"),
            (std::vector<std::string>{"https://github.com/brouk/brouk"}));
  EXPECT_TRUE(Urls("brouk news").empty());
  EXPECT_TRUE(Urls("   ").empty());
}

TEST_F(HistoryStoreTest, IgnoresCaseAndAccents) {
  Visit("https://intranet.example.cz/ucetnictvi", "Účetnictví – přehled");
  EXPECT_EQ(Urls("PREHLED").size(), 1U);
  EXPECT_EQ(Urls("účet").size(), 1U);
}

TEST_F(HistoryStoreTest, AddressStartBeatsTitleAndTypedBeatsVisited) {
  Visit("https://docs.example.com/", "Example docs");
  Visit("https://example.com/", "Home");
  EXPECT_EQ(Urls("exa")[0], "https://example.com/");
  // Typed pages rank above pages only visited through links.
  Visit("https://example.org/", "Org", kNow, VisitTransition::kTyped);
  EXPECT_EQ(Urls("exa")[0], "https://example.org/");
}

TEST_F(HistoryStoreTest, RecentAndFrequentPagesRankHigher) {
  Visit("https://a.example.com/old", "Old", kNow - 60 * kDay);
  for (int i = 0; i < 5; ++i) {
    Visit("https://a.example.com/often", "Often", kNow - 2 * kDay);
  }
  Visit("https://a.example.com/today", "Today", kNow);
  std::vector<std::string> urls = Urls("a.example");
  ASSERT_EQ(urls.size(), 3U);
  EXPECT_EQ(urls[2], "https://a.example.com/old");
  EXPECT_EQ(Urls("a.example", 2).size(), 2U);
}

TEST_F(HistoryStoreTest, OnlyTypedUrlsAreCompletedInline) {
  Visit("https://www.github.com/brouk", "Brouk", kNow, VisitTransition::kTyped);
  Visit("https://gitlab.com/", "GitLab");  // Only via links.
  std::vector<HistoryMatch> matches = store_->Suggest("git", kNow, 10);
  ASSERT_EQ(matches.size(), 2U);
  EXPECT_EQ(matches[0].url, "https://www.github.com/brouk");
  EXPECT_EQ(matches[0].inline_completion, "hub.com/brouk");
  EXPECT_EQ(matches[1].inline_completion, "");
  // With "www." or the scheme typed, the rest follows what was typed.
  EXPECT_EQ(store_->Suggest("www.git", kNow, 1)[0].inline_completion,
            "hub.com/brouk");
  EXPECT_EQ(
      store_->Suggest("https://www.github.com/b", kNow, 1)[0].inline_completion,
      "rouk");
  // Not after a space, and not for a word inside the address.
  EXPECT_EQ(store_->Suggest("git ", kNow, 1)[0].inline_completion, "");
  EXPECT_EQ(store_->Suggest("brouk", kNow, 1)[0].inline_completion, "");
}

TEST_F(HistoryStoreTest, DeletePage) {
  Visit("https://example.com/", "Example");
  EXPECT_TRUE(store_->DeletePage("https://example.com/"));
  EXPECT_FALSE(store_->DeletePage("https://example.com/"));
  EXPECT_FALSE(store_->FindPage("https://example.com/"));
  EXPECT_TRUE(Urls("example").empty());
}

TEST_F(HistoryStoreTest, RetentionRemovesOldVisitsAndEmptyPages) {
  Visit("https://old.example.com/", "Old", kNow - 100 * kDay);
  Visit("https://mixed.example.com/", "Mixed", kNow - 100 * kDay,
        VisitTransition::kTyped);
  Visit("https://mixed.example.com/", "Mixed", kNow - kDay);
  EXPECT_EQ(store_->DeleteVisitsBefore(kNow - 90 * kDay), 2);
  EXPECT_FALSE(store_->FindPage("https://old.example.com/"));
  std::optional<HistoryMatch> mixed =
      store_->FindPage("https://mixed.example.com/");
  ASSERT_TRUE(mixed);
  EXPECT_EQ(mixed->visit_count, 1);
  EXPECT_EQ(mixed->typed_count, 0);
  EXPECT_EQ(mixed->last_visit, kNow - kDay);
  EXPECT_EQ(store_->DeleteVisitsBefore(kNow - 90 * kDay), 0);
}

TEST_F(HistoryStoreTest, OpenFailsForAnUnusablePath) {
  Result<std::unique_ptr<HistoryStore>> store =
      HistoryStore::Open(dir_ / "missing-folder" / "history.db");
  EXPECT_FALSE(store);
}

TEST_F(HistoryStoreTest, RecentPagesNewestFirst) {
  Visit("https://a.com/", "A", kNow - 3 * kDay);
  Visit("https://b.com/", "B", kNow - kDay);
  Visit("https://c.com/", "C", kNow - 2 * kDay);
  std::vector<HistoryMatch> pages = store_->RecentPages(2);
  ASSERT_EQ(pages.size(), 2U);
  EXPECT_EQ(pages[0].url, "https://b.com/");
  EXPECT_EQ(pages[1].url, "https://c.com/");
}

TEST_F(HistoryStoreTest, RemoveLastVisitOfAForwardingPage) {
  Visit("https://app.example.com/", "App", kNow - kDay);
  Visit("https://app.example.com/", "App", kNow);
  Visit("https://sso.example.com/start", "", kNow);
  store_->RemoveLastVisit("https://sso.example.com/start");
  EXPECT_FALSE(store_->FindPage("https://sso.example.com/start"));
  store_->RemoveLastVisit("https://app.example.com/");
  std::optional<HistoryMatch> app =
      store_->FindPage("https://app.example.com/");
  ASSERT_TRUE(app);
  EXPECT_EQ(app->visit_count, 1);
  EXPECT_EQ(app->last_visit, kNow - kDay);
  store_->RemoveLastVisit("https://unknown.example.com/");  // No effect.
}

TEST_F(HistoryStoreTest, DeleteMatchingRemovesNeverRecordedSites) {
  Visit("https://login.microsoftonline.com/common", "Sign in");
  Visit("https://acme.okta.com/app", "Okta");
  Visit("https://example.com/", "Example");
  std::vector<UrlPattern> patterns =
      *ParseUrlPatternList("login.microsoftonline.com, *.okta.com");
  EXPECT_EQ(store_->DeleteMatching(patterns), 2);
  EXPECT_TRUE(store_->FindPage("https://example.com/"));
  EXPECT_EQ(store_->DeleteMatching({}), 0);
}

TEST_F(HistoryStoreTest, SitesAndTheirPages) {
  Visit("https://companya.atlassian.net/browse/A-1", "A-1", kNow - kDay);
  Visit("https://companya.atlassian.net/browse/A-2", "A-2", kNow - 2 * kDay);
  Visit("https://companya.atlassian.net/browse/A-1", "A-1", kNow - kDay);
  Visit("https://companyb.atlassian.net/", "B", kNow);
  Visit("file:///Users/me/a.html", "Local", kNow - 3 * kDay);
  std::vector<HistorySite> sites = store_->Sites();
  ASSERT_EQ(sites.size(), 3U);
  EXPECT_EQ(sites[0].host, "companyb.atlassian.net");  // Newest first.
  EXPECT_EQ(sites[1].host, "companya.atlassian.net");
  EXPECT_EQ(sites[1].visit_count, 3);
  EXPECT_EQ(sites[1].page_count, 2);
  EXPECT_EQ(sites[1].last_visit, kNow - kDay);
  EXPECT_EQ(sites[2].host, "");  // Local files.
  std::vector<HistoryMatch> pages =
      store_->PagesOfHost("companya.atlassian.net");
  ASSERT_EQ(pages.size(), 2U);
  EXPECT_EQ(pages[0].title, "A-1");
  EXPECT_EQ(pages[0].visit_count, 2);
}

TEST_F(HistoryStoreTest, VisitsNewestFirstInPages) {
  for (int i = 0; i < 5; ++i) {
    Visit("https://a.com/" + std::to_string(i), "", kNow - i * 60,
          i == 0 ? VisitTransition::kTyped : VisitTransition::kLink);
  }
  std::vector<HistoryVisit> first = store_->Visits(0, 3);
  ASSERT_EQ(first.size(), 3U);
  EXPECT_EQ(first[0].url, "https://a.com/0");
  EXPECT_EQ(first[0].transition, VisitTransition::kTyped);
  EXPECT_EQ(first[0].host, "a.com");
  std::vector<HistoryVisit> next = store_->Visits(first.back().time, 3);
  ASSERT_EQ(next.size(), 2U);
  EXPECT_EQ(next[0].url, "https://a.com/3");
}

TEST_F(HistoryStoreTest, DeleteHostAndVisits) {
  Visit("https://a.com/1", "", kNow - 60);
  Visit("https://a.com/1", "", kNow);
  Visit("https://b.com/", "", kNow);
  std::vector<HistoryVisit> visits = store_->Visits(0, 10);
  // The newest visit of a.com/1 only: the page stays with one visit.
  std::int64_t newest_a = 0;
  for (const HistoryVisit& visit : visits) {
    if (visit.url == "https://a.com/1" && visit.time == kNow) {
      newest_a = visit.id;
    }
  }
  std::vector<std::int64_t> ids = {newest_a};
  EXPECT_EQ(store_->DeleteVisits(ids), 1);
  EXPECT_EQ(store_->FindPage("https://a.com/1")->visit_count, 1);
  EXPECT_EQ(store_->FindPage("https://a.com/1")->last_visit, kNow - 60);
  EXPECT_EQ(store_->DeleteHost("a.com"), 1);
  EXPECT_FALSE(store_->FindPage("https://a.com/1"));
  EXPECT_TRUE(store_->FindPage("https://b.com/"));
}

TEST_F(HistoryStoreTest, SearchCountsOnlyMatchingVisits) {
  Visit("https://companya.atlassian.net/browse/A-1", "Sprint board",
        kNow - 10 * kDay);
  Visit("https://companya.atlassian.net/browse/A-1", "Sprint board", kNow,
        VisitTransition::kTyped);
  Visit("https://companya.atlassian.net/wiki", "Wiki", kNow);
  Visit("https://example.com/", "Example", kNow);
  HistoryQuery recent =
      HistoryQuery::Parse("site:atlassian.net after:3d", kNow);
  std::vector<HistorySite> sites = store_->SearchSites(recent, "Main");
  ASSERT_EQ(sites.size(), 1U);
  EXPECT_EQ(sites[0].host, "companya.atlassian.net");
  EXPECT_EQ(sites[0].visit_count, 2);  // The old visit does not match.
  EXPECT_EQ(sites[0].page_count, 2);
  std::vector<HistoryMatch> pages = store_->SearchPagesOfHost(
      "companya.atlassian.net", HistoryQuery::Parse("sprint", kNow), "Main");
  ASSERT_EQ(pages.size(), 1U);
  EXPECT_EQ(pages[0].visit_count, 2);
  std::vector<HistoryVisit> typed = store_->SearchVisits(
      HistoryQuery::Parse("type:typed", kNow), "Main", 0, 10);
  ASSERT_EQ(typed.size(), 1U);
  EXPECT_EQ(typed[0].url, "https://companya.atlassian.net/browse/A-1");
  EXPECT_EQ(store_
                ->SearchVisits(HistoryQuery::Parse("profile:main", kNow),
                               "Main", 0, 2)
                .size(),
            2U);
  EXPECT_TRUE(
      store_->SearchSites(HistoryQuery::Parse("profile:work", kNow), "Main")
          .empty());
}

TEST_F(HistoryStoreTest, CleanUpByHostAndTime) {
  Visit("https://a.com/1", "", kNow - 10 * kDay);
  Visit("https://a.com/1", "", kNow - 60);
  Visit("https://a.com/2", "", kNow - 60);
  Visit("https://b.com/", "", kNow - 120);
  Visit("https://c.com/", "", kNow - 20 * kDay);
  std::vector<HistorySite> week = store_->HostsSince(kNow - 7 * kDay);
  ASSERT_EQ(week.size(), 2U);
  EXPECT_EQ(week[0].host, "a.com");  // Most visits first.
  EXPECT_EQ(week[0].visit_count, 2);
  EXPECT_EQ(week[0].page_count, 2);
  EXPECT_EQ(store_->HostsSince(0).size(), 3U);
  // Delete a.com's visits of the last week: the older visit stays.
  std::vector<std::string> hosts = {"a.com"};
  EXPECT_EQ(store_->DeleteHostVisitsSince(hosts, kNow - 7 * kDay), 2);
  std::optional<HistoryMatch> page = store_->FindPage("https://a.com/1");
  ASSERT_TRUE(page);
  EXPECT_EQ(page->visit_count, 1);
  EXPECT_FALSE(store_->FindPage("https://a.com/2"));
  EXPECT_TRUE(store_->FindPage("https://b.com/"));
  // All time.
  EXPECT_EQ(store_->DeleteHostVisitsSince(hosts, 0), 1);
  EXPECT_FALSE(store_->FindPage("https://a.com/1"));
}

TEST_F(HistoryStoreTest, ClearVisitsSince) {
  Visit("https://a.com/", "", kNow - 3 * kDay);
  Visit("https://a.com/", "", kNow - 60);
  Visit("https://b.com/", "", kNow - 30);
  EXPECT_EQ(store_->CountVisitsSince(kNow - kDay), 2);
  EXPECT_EQ(store_->CountVisitsSince(0), 3);
  EXPECT_EQ(store_->DeleteVisitsSince(kNow - kDay), 2);
  EXPECT_EQ(store_->FindPage("https://a.com/")->visit_count, 1);
  EXPECT_FALSE(store_->FindPage("https://b.com/"));
  EXPECT_EQ(store_->DeleteVisitsSince(0), 1);
  EXPECT_FALSE(store_->FindPage("https://a.com/"));
}

}  // namespace
}  // namespace brouk
