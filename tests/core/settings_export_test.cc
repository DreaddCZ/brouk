#include "core/settings_export.h"

#include <string>

#include <gtest/gtest.h>

namespace brouk {
namespace {

TEST(SettingsExportTest, RoundTrip) {
  Settings global(SettingScope::kGlobal);
  ASSERT_FALSE(global.Set("search.engine", std::string("ecosia")));
  ASSERT_FALSE(global.Set("downloads.ask_where_to_save", true));
  std::string file = ExportSettings(global, "2026-10-22T10:00:00Z");
  Result<ImportedSettings> imported = ParseSettingsExport(file);
  ASSERT_TRUE(imported) << imported.error().message;
  EXPECT_TRUE(imported.value().problems.empty());
  EXPECT_EQ(imported.value().settings, global);
}

TEST(SettingsExportTest, RefusesOtherFiles) {
  EXPECT_FALSE(ParseSettingsExport("format: brouk-profile\nversion: 1\n"));
  EXPECT_FALSE(ParseSettingsExport("format: brouk-settings\nversion: 1\n"));
  EXPECT_FALSE(
      ParseSettingsExport("format: brouk-settings\nversion: 9\n"
                          "settings: {}\n"));
  EXPECT_FALSE(ParseSettingsExport("{{"));
}

TEST(SettingsExportTest, InvalidValuesAreListed) {
  Result<ImportedSettings> imported = ParseSettingsExport(
      "format: brouk-settings\nversion: 1\nsettings:\n"
      "  search:\n    engine: altavista\n");
  ASSERT_TRUE(imported);
  ASSERT_EQ(imported.value().problems.size(), 1u);
  EXPECT_EQ(imported.value().settings.GetString("search.engine"), "google");
}

}  // namespace
}  // namespace brouk
