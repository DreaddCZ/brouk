#include "core/adblock_store.h"

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

namespace brouk {
namespace {

constexpr std::int64_t kHour = 3600;

class AdblockStoreTest : public testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("brouk-adblock-test-" +
            std::string(
                testing::UnitTest::GetInstance()->current_test_info()->name()));
    std::error_code error;
    std::filesystem::remove_all(dir_, error);
  }
  void TearDown() override {
    std::error_code error;
    std::filesystem::remove_all(dir_, error);
  }
  static FilterListDef List(std::string id, bool hosts = false) {
    return {.id = std::move(id),
            .name = "",
            .category = FilterCategory::kAds,
            .url = "https://example.com/list.txt",
            .hosts_format = hosts,
            .trusted = false,
            .default_on = true,
            .issues_url = ""};
  }
  std::filesystem::path dir_;
};

TEST_F(AdblockStoreTest, SavesDownloadsAndTheirState) {
  AdblockStore store(dir_);
  EXPECT_TRUE(store.IsDue("easylist", 1000));
  ASSERT_FALSE(store.SaveDownload("easylist",
                                  "! Expires: 2 days\n||ads.example^\n", 1000));
  EXPECT_EQ(store.ReadList("easylist"), "! Expires: 2 days\n||ads.example^\n");
  EXPECT_FALSE(store.IsDue("easylist", 1000 + 47 * kHour));
  EXPECT_TRUE(store.IsDue("easylist", 1000 + 48 * kHour));
  // State survives a restart.
  AdblockStore again(dir_);
  ASSERT_EQ(again.states().count("easylist"), 1U);
  EXPECT_EQ(again.states().at("easylist").updated, 1000);
  EXPECT_EQ(again.states().at("easylist").expires_hours, 48);
}

TEST_F(AdblockStoreTest, AFailedUpdateKeepsThePreviousVersion) {
  AdblockStore store(dir_);
  ASSERT_FALSE(store.SaveDownload("easylist", "||ads.example^\n", 1000));
  EXPECT_TRUE(store.SaveDownload(
      "easylist", "<!doctype html><title>Wi-Fi login</title>", 2000));
  EXPECT_EQ(store.ReadList("easylist"), "||ads.example^\n");
  EXPECT_EQ(store.states().at("easylist").updated, 1000);
  EXPECT_FALSE(store.states().at("easylist").error.empty());
  store.SaveError("easylist", "Network unreachable", 3000);
  EXPECT_EQ(store.states().at("easylist").error, "Network unreachable");
  // Waits an hour after a failure, even when the list is old.
  EXPECT_FALSE(store.IsDue("easylist", 3000 + 1000 * kHour / 1000));
  EXPECT_TRUE(store.IsDue("easylist", 3000 + 96 * kHour));
  EXPECT_TRUE(store.SaveDownload("../evil", "||a^", 1));
}

TEST_F(AdblockStoreTest, BuildsAndCachesEngines) {
  AdblockStore store(dir_);
  std::vector<FilterListDef> lists = {List("ads"), List("hosts", true),
                                      List("missing")};
  EXPECT_FALSE(store.LoadOrBuildEngine(lists, "").engine)
      << "nothing downloaded yet";
  ASSERT_FALSE(store.SaveDownload("ads", "||ads.example^\n", 1));
  ASSERT_FALSE(store.SaveDownload("hosts", "0.0.0.0 tracker.example\n", 1));
  ASSERT_FALSE(store.SaveMyFilters("@@||ads.example/ok.js\n"));
  BuiltEngine built = store.LoadOrBuildEngine(lists, "");
  ASSERT_TRUE(built.engine);
  EXPECT_EQ(built.list_ids,
            (std::vector<std::string>{"ads", "hosts", "my-filters"}));
  AdblockMatch tracker = built.engine->Check("https://tracker.example/x.js",
                                             "https://site.example/", "script");
  EXPECT_TRUE(tracker.block);
  EXPECT_EQ(tracker.filter->list, 1);
  AdblockMatch mine = built.engine->Check("https://ads.example/ok.js",
                                          "https://site.example/", "script");
  EXPECT_FALSE(mine.block);
  EXPECT_EQ(built.list_ids[mine.exception->list], "my-filters");
  // The second time it comes from the cache, with the same behaviour.
  int cached = 0;
  for (const auto& entry :
       std::filesystem::directory_iterator(dir_ / "engines")) {
    cached += entry.path().extension() == ".dat" ? 1 : 0;
  }
  EXPECT_EQ(cached, 1);
  BuiltEngine again = AdblockStore(dir_).LoadOrBuildEngine(lists, "");
  ASSERT_TRUE(again.engine);
  EXPECT_TRUE(
      again.engine
          ->Check("https://ads.example/a.js", "https://s.example/", "script")
          .block);
}

TEST_F(AdblockStoreTest, RemovesUnusedLists) {
  AdblockStore store(dir_);
  ASSERT_FALSE(store.SaveDownload("a", "||a.example^\n", 1));
  ASSERT_FALSE(store.SaveDownload("b", "||b.example^\n", 1));
  std::vector<std::string> used = {"a"};
  store.RemoveUnused(used);
  EXPECT_TRUE(store.ReadList("a"));
  EXPECT_FALSE(store.ReadList("b"));
  EXPECT_EQ(store.states().count("b"), 0U);
}

TEST_F(AdblockStoreTest, ResourcesMustBeJson) {
  AdblockStore store(dir_);
  EXPECT_FALSE(store.ReadResources());
  EXPECT_TRUE(store.SaveResources("<html>", 1));
  EXPECT_FALSE(store.SaveResources("[]", 2));
  EXPECT_EQ(store.ReadResources(), "[]");
}

TEST(LooksLikeFilterListTest, WebPagesAreNotLists) {
  EXPECT_TRUE(LooksLikeFilterList("[Adblock Plus 2.0]\n||a^"));
  EXPECT_TRUE(LooksLikeFilterList("\n# hosts\n0.0.0.0 a.example"));
  EXPECT_FALSE(LooksLikeFilterList("  <html><body>"));
  EXPECT_FALSE(LooksLikeFilterList("{\"error\": 1}"));
  EXPECT_FALSE(LooksLikeFilterList(""));
}

}  // namespace
}  // namespace brouk
