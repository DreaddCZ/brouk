#include "core/history_store.h"

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

}  // namespace
}  // namespace brouk
