#include "core/utf8_path.h"

#include <filesystem>
#include <string>
#include <system_error>

#include <gtest/gtest.h>

#include "core/sqlite_db.h"

namespace brouk {
namespace {

TEST(Utf8PathTest, RoundTripsNonAsciiNames) {
  const std::string text = "Ji\xC5\x99\xC3\xAD/\xE6\x97\xA5\xE6\x9C\xAC/a.db";
  const std::filesystem::path path = PathFromUtf8(text);
  EXPECT_EQ(path.filename().u8string(), u8"a.db");
  EXPECT_EQ(PathToUtf8(path), text);
  EXPECT_EQ(PathToUtf8(path.parent_path().parent_path()), "Ji\xC5\x99\xC3\xAD");
}

TEST(Utf8PathTest, RefusesTextThatIsNotUtf8) {
  EXPECT_TRUE(PathFromUtf8("Ji\xF8\xED").empty());        // Windows-1250.
  EXPECT_TRUE(PathFromUtf8("\xC0\xAF").empty());          // Overlong "/".
  EXPECT_TRUE(PathFromUtf8("\xED\xA0\x80").empty());      // A surrogate.
  EXPECT_TRUE(PathFromUtf8("\xF4\x90\x80\x80").empty());  // Above U+10FFFF.
  EXPECT_TRUE(PathFromUtf8("ab\xE2\x82").empty());        // Cut short.
  EXPECT_EQ(PathToUtf8(PathFromUtf8("plain")), "plain");
}

// The bug this fixes: on Windows a database in a folder whose name the
// system's code page cannot write (as many user folders) did not open.
TEST(Utf8PathTest, SqliteOpensInNonAsciiFolder) {
  std::error_code error;
  const std::filesystem::path folder =
      std::filesystem::temp_directory_path(error) /
      PathFromUtf8("brouk-utf8-\xC5\x99\xE6\x97\xA5-test");
  ASSERT_FALSE(error) << error.message();
  std::filesystem::create_directories(folder, error);
  ASSERT_FALSE(error) << error.message();
  {
    Result<std::unique_ptr<SqliteDatabase>> db =
        SqliteDatabase::Open(folder / "test.db");
    ASSERT_TRUE(db) << db.error().message;
  }
  EXPECT_TRUE(std::filesystem::exists(folder / "test.db", error));
  std::filesystem::remove_all(folder, error);
}

}  // namespace
}  // namespace brouk
