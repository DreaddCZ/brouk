#include "core/history_sql.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include "core/history_store.h"

namespace brouk {
namespace {

class HistorySqlTest : public testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("brouk-sql-test-" +
            std::string(
                testing::UnitTest::GetInstance()->current_test_info()->name()));
    std::error_code error;
    std::filesystem::remove_all(dir_, error);
    std::filesystem::create_directories(dir_ / "work dir's");
    auto main = HistoryStore::Open(dir_ / "main.db");
    ASSERT_TRUE(main);
    main.value()->RecordVisit("https://example.com/", "Example", 1000,
                              VisitTransition::kTyped);
    main.value()->RecordVisit("https://example.com/", "Example", 2000,
                              VisitTransition::kLink);
    // A path with a space and a quote, and a name with a quote.
    auto work = HistoryStore::Open(dir_ / "work dir's" / "history.db");
    ASSERT_TRUE(work);
    work.value()->RecordVisit("https://jira.example.com/a", "Jira", 3000,
                              VisitTransition::kLink);
    sources_ = {{"Main", dir_ / "main.db"},
                {"Bob's work", dir_ / "work dir's" / "history.db"},
                {"Missing", dir_ / "missing.db"}};
  }
  void TearDown() override {
    std::error_code error;
    std::filesystem::remove_all(dir_, error);
  }
  Result<SqlResult> Run(std::string_view sql, SqlLimits limits = {}) {
    return RunHistorySql(sources_, sql, limits);
  }

  std::filesystem::path dir_;
  std::vector<SqlHistorySource> sources_;
};

TEST_F(HistorySqlTest, ViewsCoverEveryProfile) {
  Result<SqlResult> pages =
      Run("SELECT profile, url, visit_count, typed_count FROM pages "
          "ORDER BY profile");
  ASSERT_TRUE(pages) << pages.error().message;
  ASSERT_EQ(pages.value().columns,
            (std::vector<std::string>{"profile", "url", "visit_count",
                                      "typed_count"}));
  ASSERT_EQ(pages.value().rows.size(), 2U);
  EXPECT_EQ(pages.value().rows[0][0].text, "Bob's work");
  EXPECT_EQ(pages.value().rows[1][2].text, "2");
  EXPECT_EQ(pages.value().rows[1][2].type, SqlCell::Type::kInteger);

  Result<SqlResult> visits =
      Run("SELECT transition, count(*) FROM visits GROUP BY transition "
          "ORDER BY transition");
  ASSERT_TRUE(visits) << visits.error().message;
  ASSERT_EQ(visits.value().rows.size(), 2U);
  EXPECT_EQ(visits.value().rows[0][0].text, "link");
  EXPECT_EQ(visits.value().rows[0][1].text, "2");
  EXPECT_EQ(visits.value().rows[1][0].text, "typed");

  Result<SqlResult> time = Run("SELECT visit_time FROM visits LIMIT 1");
  ASSERT_TRUE(time) << time.error().message;
  EXPECT_EQ(time.value().rows[0][0].text.size(), 19U);  // YYYY-MM-DD HH:MM:SS
}

TEST_F(HistorySqlTest, CommonTableExpressionsAndNulls) {
  Result<SqlResult> result =
      Run("WITH hosts AS (SELECT DISTINCT host FROM pages) "
          "SELECT host, NULL FROM hosts ORDER BY host");
  ASSERT_TRUE(result) << result.error().message;
  EXPECT_EQ(result.value().rows.size(), 2U);
  EXPECT_EQ(result.value().rows[0][1].type, SqlCell::Type::kNull);
}

TEST_F(HistorySqlTest, OnlyReadingThroughTheViews) {
  for (std::string_view sql :
       {"DELETE FROM pages", "UPDATE pages SET title = 'x'",
        "INSERT INTO pages VALUES (1)", "DROP VIEW pages",
        "PRAGMA table_info(pages)", "ATTACH 'x.db' AS x",
        "CREATE TABLE t (a)"}) {
    Result<SqlResult> result = Run(sql);
    EXPECT_FALSE(result) << sql;
  }
  // Direct access to the tables or the schema is not allowed.
  EXPECT_FALSE(Run("SELECT * FROM p0.urls"));
  EXPECT_FALSE(Run("SELECT * FROM sqlite_master"));
  EXPECT_FALSE(Run("SELECT * FROM p0.sqlite_master"));
  // One statement only.
  Result<SqlResult> two = Run("SELECT 1; DELETE FROM pages");
  ASSERT_FALSE(two);
  EXPECT_EQ(two.error().message, "Only one statement can be run at a time.");
  EXPECT_TRUE(Run("SELECT 1;  "));
  // SQLite's own message for mistakes.
  Result<SqlResult> wrong = Run("SELECT nope FROM pages");
  ASSERT_FALSE(wrong);
  EXPECT_NE(wrong.error().message.find("no such column"), std::string::npos);
}

TEST_F(HistorySqlTest, RowAndTimeLimits) {
  Result<SqlResult> many =
      Run("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n "
          "WHERE i < 5000) SELECT i FROM n",
          {.max_rows = 1000,
           .max_time = std::chrono::milliseconds(2000),
           .max_cell_bytes = 1024});
  ASSERT_TRUE(many) << many.error().message;
  EXPECT_EQ(many.value().rows.size(), 1000U);
  EXPECT_TRUE(many.value().truncated);

  Result<SqlResult> endless =
      Run("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n) "
          "SELECT count(*) FROM n",
          {.max_rows = 1000,
           .max_time = std::chrono::milliseconds(100),
           .max_cell_bytes = 1024});
  ASSERT_FALSE(endless) << endless.value().rows.size();
  EXPECT_NE(endless.error().message.find("was stopped"), std::string::npos)
      << endless.error().message;
}

TEST_F(HistorySqlTest, TheDatabasesAreNotChanged) {
  std::filesystem::file_time_type before =
      std::filesystem::last_write_time(dir_ / "main.db");
  ASSERT_TRUE(Run("SELECT * FROM visits"));
  EXPECT_EQ(std::filesystem::last_write_time(dir_ / "main.db"), before);
}

}  // namespace
}  // namespace brouk
