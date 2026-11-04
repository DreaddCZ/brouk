#include "core/profile_export.h"

#include <string>

#include <gtest/gtest.h>

namespace brouk {
namespace {

Settings WorkSettings() {
  Settings settings(SettingScope::kProfile);
  EXPECT_FALSE(settings.Set("profile.name", std::string("Work: \"A\"")));
  EXPECT_FALSE(settings.Set("profile.color", std::string("green")));
  return settings;
}

TEST(ProfileExportTest, RoundTrip) {
  std::string file = ExportProfile(WorkSettings(), "2026-10-22T10:00:00Z");
  EXPECT_NE(file.find("format: brouk-profile"), std::string::npos) << file;
  EXPECT_NE(file.find("version: 1"), std::string::npos);
  Result<ImportedProfile> imported = ParseProfileExport(file);
  ASSERT_TRUE(imported) << imported.error().message;
  EXPECT_TRUE(imported.value().problems.empty());
  EXPECT_EQ(imported.value().settings, WorkSettings());
}

TEST(ProfileExportTest, ContainsNothingButSettings) {
  std::string file = ExportProfile(WorkSettings(), "t");
  for (const char* word : {"cookie", "Cookie", "session", "cache"}) {
    EXPECT_EQ(file.find(std::string(word) + ":"), std::string::npos) << word;
  }
}

TEST(ProfileExportTest, RefusesOtherFiles) {
  EXPECT_FALSE(ParseProfileExport(""));
  EXPECT_FALSE(ParseProfileExport("profile:\n  name: x\n"));
  EXPECT_FALSE(ParseProfileExport("format: something-else\nversion: 1\n"));
  EXPECT_FALSE(ParseProfileExport("format: brouk-profile\n"));
  EXPECT_FALSE(ParseProfileExport("format: brouk-profile\nversion: zero\n"));
  EXPECT_FALSE(ParseProfileExport("[unclosed"));
}

TEST(ProfileExportTest, RefusesNewerVersions) {
  Result<ImportedProfile> imported =
      ParseProfileExport("format: brouk-profile\nversion: 2\n");
  ASSERT_FALSE(imported);
  EXPECT_NE(imported.error().message.find("newer version"), std::string::npos);
}

TEST(ProfileExportTest, ReportsWhatIsNotImported) {
  Result<ImportedProfile> imported = ParseProfileExport(
      "format: brouk-profile\n"
      "version: 1\n"
      "settings:\n"
      "  profile:\n"
      "    name: Home\n"
      "    color: magenta\n"
      "downloads: []\n");
  ASSERT_TRUE(imported);
  ASSERT_EQ(imported.value().problems.size(), 2u);
  EXPECT_NE(imported.value().problems[0].find("profile.color"),
            std::string::npos);
  EXPECT_NE(imported.value().problems[1].find("downloads"), std::string::npos);
  EXPECT_EQ(imported.value().settings.GetString("profile.name"), "Home");
  EXPECT_EQ(imported.value().settings.GetString("profile.color"), "blue");
}

// History (022): written only when chosen, read back as its own section.
TEST(ProfileExportTest, KeepsTheHistorySection) {
  Settings settings(SettingScope::kProfile);
  EXPECT_EQ(ExportProfile(settings, "2026-11-04T10:00:00Z").find("\nhistory:"),
            std::string::npos);
  YamlNode history;
  history.kind = YamlNode::Kind::kSequence;
  YamlNode page = YamlNode::Map();
  page.GetOrAdd("url") = YamlNode::Scalar("https://example.com/", false);
  history.items.push_back(page);
  Result<ImportedProfile> imported = ParseProfileExport(ExportProfile(
      settings, "2026-11-04T10:00:00Z", YamlNode(), YamlNode(), history));
  ASSERT_TRUE(imported);
  EXPECT_TRUE(imported.value().problems.empty());
  ASSERT_TRUE(imported.value().history);
  ASSERT_EQ(imported.value().history->items.size(), 1u);
}

}  // namespace
}  // namespace brouk
