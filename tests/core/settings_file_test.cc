#include "core/settings_file.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <gtest/gtest.h>

namespace brouk {
namespace {

class SettingsFileTest : public testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("brouk-settings-test-" +
            std::string(
                testing::UnitTest::GetInstance()->current_test_info()->name()));
    std::error_code error;
    std::filesystem::remove_all(dir_, error);
  }
  void TearDown() override {
    std::error_code error;
    std::filesystem::remove_all(dir_, error);
  }
  void Write(const std::string& text) {
    std::filesystem::create_directories(dir_);
    std::ofstream(path()) << text;
  }
  std::filesystem::path path() const { return dir_ / "settings.yaml"; }

  std::filesystem::path dir_;
};

TEST_F(SettingsFileTest, MissingFileGivesDefaults) {
  LoadedSettings loaded = LoadSettingsFile(path(), SettingScope::kGlobal);
  EXPECT_TRUE(loaded.problems.empty());
  EXPECT_EQ(loaded.settings, Settings(SettingScope::kGlobal));
}

TEST_F(SettingsFileTest, BrokenFileGivesDefaultsAndAProblem) {
  Write("profile: [\n");
  LoadedSettings loaded = LoadSettingsFile(path(), SettingScope::kProfile);
  EXPECT_EQ(loaded.problems.size(), 1u);
  EXPECT_EQ(loaded.settings, Settings(SettingScope::kProfile));
}

TEST_F(SettingsFileTest, SavesCreatesDirectoriesAndReadsBack) {
  Settings settings(SettingScope::kProfile);
  ASSERT_FALSE(settings.Set("profile.name", std::string("Klient A")));
  std::filesystem::path nested = dir_ / "profiles" / "p1" / "settings.yaml";
  ASSERT_FALSE(SaveSettingsFile(nested, settings));
  LoadedSettings loaded = LoadSettingsFile(nested, SettingScope::kProfile);
  EXPECT_TRUE(loaded.problems.empty());
  EXPECT_EQ(loaded.settings, settings);
  EXPECT_FALSE(std::filesystem::exists(nested.string() + ".tmp"));
  auto perms = std::filesystem::status(nested).permissions();
  EXPECT_EQ(perms & std::filesystem::perms::group_read,
            std::filesystem::perms::none);
}

TEST_F(SettingsFileTest, SaveReplacesHandEditedFile) {
  Write("# my comment\nprofile:\n  color: red\n  bogus: 1\n");
  LoadedSettings loaded = LoadSettingsFile(path(), SettingScope::kProfile);
  EXPECT_EQ(loaded.problems.size(), 1u);
  ASSERT_FALSE(SaveSettingsFile(path(), loaded.settings));
  LoadedSettings again = LoadSettingsFile(path(), SettingScope::kProfile);
  EXPECT_TRUE(again.problems.empty());
  EXPECT_EQ(again.settings.GetString("profile.color"), "red");
}

}  // namespace
}  // namespace brouk
