#include "core/history_profile.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "core/yaml_io.h"

namespace brouk {
namespace {

constexpr std::int64_t kDay = 24 * 60 * 60;
constexpr std::int64_t kNow = 1'800'000'000;

class HistoryProfileTest : public testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("brouk-history-profile-test-" +
            std::string(
                testing::UnitTest::GetInstance()->current_test_info()->name()));
    std::error_code error;
    std::filesystem::remove_all(dir_, error);
    std::filesystem::create_directories(dir_);
    source_ = Open("source.db");
    target_ = Open("target.db");
  }
  void TearDown() override {
    source_.reset();
    target_.reset();
    std::error_code error;
    std::filesystem::remove_all(dir_, error);
  }
  std::unique_ptr<HistoryStore> Open(const std::string& name) {
    Result<std::unique_ptr<HistoryStore>> store =
        HistoryStore::Open(dir_ / name);
    EXPECT_TRUE(store) << (store ? "" : store.error().message);
    return store ? std::move(store).value() : nullptr;
  }
  void Visit(const std::string& url,
             std::int64_t time,
             VisitTransition transition = VisitTransition::kLink) {
    ASSERT_FALSE(
        source_->RecordVisit(url, "Title of " + url, time, transition));
  }
  // The section as it goes through the file.
  static YamlNode ThroughFile(const YamlNode& section) {
    YamlNode root = YamlNode::Map();
    root.GetOrAdd("history") = section;
    Result<YamlNode> parsed = ParseYaml(EmitYaml(root));
    EXPECT_TRUE(parsed);
    return parsed ? *parsed.value().Find("history") : YamlNode();
  }

  std::filesystem::path dir_;
  std::unique_ptr<HistoryStore> source_;
  std::unique_ptr<HistoryStore> target_;
};

TEST_F(HistoryProfileTest, ExportsTheLastDaysAndImportsThem) {
  Visit("https://old.example.com/", kNow - 40 * kDay);
  Visit("https://example.com/a", kNow - 10 * kDay, VisitTransition::kTyped);
  Visit("https://example.com/a", kNow - kDay);
  Visit("https://example.com/b", kNow - 2 * kDay);
  const YamlNode section = ExportProfileHistory(
      *source_, {.kind = HistoryExportLimit::Kind::kDays, .count = 30}, kNow);
  ASSERT_EQ(section.items.size(), 2u);  // a (newest), b; not old.

  const ProfileHistoryImport imported =
      ImportProfileHistory(*target_, ThroughFile(section), std::nullopt);
  EXPECT_TRUE(imported.problems.empty());
  EXPECT_EQ(imported.pages, 2);
  EXPECT_EQ(imported.visits, 3);
  const std::optional<HistoryMatch> a =
      target_->FindPage("https://example.com/a");
  ASSERT_TRUE(a);
  EXPECT_EQ(a->title, "Title of https://example.com/a");
  EXPECT_EQ(a->visit_count, 2);
  EXPECT_EQ(a->typed_count, 1);  // How the visit started is kept.
  EXPECT_EQ(a->last_visit, kNow - kDay);
  EXPECT_FALSE(target_->FindPage("https://old.example.com/"));
  // Suggested in the address bar.
  const std::vector<HistoryMatch> suggested =
      target_->Suggest("example.com/a", kNow, 5);
  ASSERT_FALSE(suggested.empty());
  EXPECT_EQ(suggested.front().url, "https://example.com/a");
}

TEST_F(HistoryProfileTest, ExportsTheLastPagesWithAllTheirVisits) {
  Visit("https://example.com/old", kNow - 50 * kDay);
  Visit("https://example.com/a", kNow - 40 * kDay);
  Visit("https://example.com/b", kNow - 2 * kDay);
  Visit("https://example.com/a", kNow - kDay);
  const YamlNode section = ExportProfileHistory(
      *source_, {.kind = HistoryExportLimit::Kind::kPages, .count = 2}, kNow);
  ASSERT_EQ(section.items.size(), 2u);
  ImportProfileHistory(*target_, ThroughFile(section), std::nullopt);
  const std::optional<HistoryMatch> a =
      target_->FindPage("https://example.com/a");
  ASSERT_TRUE(a);
  EXPECT_EQ(a->visit_count, 2);  // Also the visit 40 days ago.
  EXPECT_TRUE(target_->FindPage("https://example.com/b"));
  EXPECT_FALSE(target_->FindPage("https://example.com/old"));
}

TEST_F(HistoryProfileTest, TheRetentionLimitStillApplies) {
  Visit("https://example.com/a", kNow - 40 * kDay);
  Visit("https://example.com/b", kNow - kDay);
  const YamlNode section = ExportProfileHistory(
      *source_, {.kind = HistoryExportLimit::Kind::kPages, .count = 10}, kNow);
  const ProfileHistoryImport imported =
      ImportProfileHistory(*target_, ThroughFile(section), kNow - 30 * kDay);
  EXPECT_EQ(imported.visits, 1);
  EXPECT_FALSE(target_->FindPage("https://example.com/a"));
  EXPECT_TRUE(target_->FindPage("https://example.com/b"));
}

TEST_F(HistoryProfileTest, SkipsInvalidEntries) {
  Result<YamlNode> parsed = ParseYaml(
      "history:\n"
      "  - url: \"brouk://settings\"\n"
      "    visits: [{time: 1700000000, how: typed}]\n"
      "  - title: no address\n"
      "  - url: \"https://example.com/\"\n"
      "    visits:\n"
      "      - {time: soon, how: typed}\n"
      "      - {time: 1700000000, how: teleported}\n"
      "      - {time: 1700000001, how: bookmark}\n"
      "  - url: \"https://example.org/\"\n"
      "    visits: not a list\n");
  ASSERT_TRUE(parsed);
  const ProfileHistoryImport imported = ImportProfileHistory(
      *target_, *parsed.value().Find("history"), std::nullopt);
  EXPECT_EQ(imported.pages, 1);
  EXPECT_EQ(imported.visits, 1);
  EXPECT_EQ(imported.problems.size(), 5u);
  EXPECT_FALSE(target_->FindPage("brouk://settings"));
  EXPECT_TRUE(target_->FindPage("https://example.com/"));
}

TEST_F(HistoryProfileTest, ASectionThatIsNotAListImportsNothing) {
  const ProfileHistoryImport imported = ImportProfileHistory(
      *target_, YamlNode::Scalar("nothing", true), std::nullopt);
  EXPECT_EQ(imported.visits, 0);
  ASSERT_EQ(imported.problems.size(), 1u);
}

TEST(HistoryKeepSinceTest, FollowsTheSetting) {
  EXPECT_EQ(HistoryKeepSince("90", kNow), kNow - 90 * kDay);
  EXPECT_FALSE(HistoryKeepSince("forever", kNow));
  EXPECT_FALSE(HistoryKeepSince("", kNow));
}

}  // namespace
}  // namespace brouk
