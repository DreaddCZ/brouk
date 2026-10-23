#include "core/bookmark_file.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <gtest/gtest.h>

namespace brouk {
namespace {

class BookmarkFileTest : public testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("brouk-bookmarks-test-" +
            std::string(
                testing::UnitTest::GetInstance()->current_test_info()->name()));
    std::error_code error;
    std::filesystem::remove_all(dir_, error);
  }
  void TearDown() override {
    std::error_code error;
    std::filesystem::remove_all(dir_, error);
  }
  std::filesystem::path path() const { return dir_ / "bookmarks.json"; }
  std::filesystem::path backup() const { return dir_ / "bookmarks.json.bak"; }

  static BookmarkModel WithPage(const std::string& url) {
    BookmarkModel model;
    EXPECT_TRUE(model.AddBookmark(BookmarkModel::kBarId, BookmarkModel::kEnd,
                                  "Page", url, "", 1));
    return model;
  }
  static std::string FirstUrl(const BookmarkModel& model) {
    const BookmarkItem* bar = model.Find(BookmarkModel::kBarId);
    return bar->children.empty() ? "" : model.Find(bar->children[0])->url;
  }

  std::filesystem::path dir_;
};

TEST_F(BookmarkFileTest, MissingFileGivesAnEmptyTree) {
  LoadedBookmarks loaded = LoadBookmarks(path());
  EXPECT_FALSE(loaded.problem);
  EXPECT_TRUE(loaded.model.Find(BookmarkModel::kBarId)->children.empty());
}

TEST_F(BookmarkFileTest, SavesAndLoads) {
  ASSERT_FALSE(SaveBookmarks(path(), WithPage("https://a.com/")));
  LoadedBookmarks loaded = LoadBookmarks(path());
  EXPECT_FALSE(loaded.problem);
  EXPECT_EQ(FirstUrl(loaded.model), "https://a.com/");
  EXPECT_FALSE(std::filesystem::exists(dir_ / "bookmarks.json.tmp"));
}

TEST_F(BookmarkFileTest, KeepsThePreviousVersion) {
  ASSERT_FALSE(SaveBookmarks(path(), WithPage("https://a.com/")));
  ASSERT_FALSE(SaveBookmarks(path(), WithPage("https://b.com/")));
  EXPECT_EQ(FirstUrl(LoadBookmarks(path()).model), "https://b.com/");
  EXPECT_EQ(FirstUrl(LoadBookmarks(backup()).model), "https://a.com/");
}

TEST_F(BookmarkFileTest, DamagedFileFallsBackToTheBackup) {
  ASSERT_FALSE(SaveBookmarks(path(), WithPage("https://a.com/")));
  ASSERT_FALSE(SaveBookmarks(path(), WithPage("https://b.com/")));
  std::ofstream(path(), std::ios::trunc) << "{\"roots\": {";  // Half written.
  LoadedBookmarks loaded = LoadBookmarks(path());
  ASSERT_TRUE(loaded.problem);
  EXPECT_EQ(FirstUrl(loaded.model), "https://a.com/");
}

TEST_F(BookmarkFileTest, CrashBeforeRenameLeavesTheBackup) {
  ASSERT_FALSE(SaveBookmarks(path(), WithPage("https://a.com/")));
  std::filesystem::rename(path(), backup());
  LoadedBookmarks loaded = LoadBookmarks(path());
  EXPECT_TRUE(loaded.problem);
  EXPECT_EQ(FirstUrl(loaded.model), "https://a.com/");
}

TEST_F(BookmarkFileTest, NothingReadableGivesAnEmptyTree) {
  std::filesystem::create_directories(dir_);
  std::ofstream(path()) << "not json";
  LoadedBookmarks loaded = LoadBookmarks(path());
  EXPECT_TRUE(loaded.problem);
  EXPECT_TRUE(loaded.model.Find(BookmarkModel::kBarId)->children.empty());
}

}  // namespace
}  // namespace brouk
