#include "core/profile_registry.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <gtest/gtest.h>

namespace brouk {
namespace {

class ProfileRegistryTest : public testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("brouk-profiles-test-" +
            std::string(
                testing::UnitTest::GetInstance()->current_test_info()->name()));
    std::error_code error;
    std::filesystem::remove_all(dir_, error);
  }
  void TearDown() override {
    std::error_code error;
    std::filesystem::remove_all(dir_, error);
  }
  void Write(const std::filesystem::path& relative, const std::string& text) {
    std::filesystem::create_directories((dir_ / relative).parent_path());
    std::ofstream(dir_ / relative) << text;
  }

  std::filesystem::path dir_;
};

TEST(ProfileIdTest, Validation) {
  EXPECT_TRUE(IsValidProfileId("default"));
  EXPECT_TRUE(IsValidProfileId("p-0a1b2c3d"));
  EXPECT_FALSE(IsValidProfileId(""));
  EXPECT_FALSE(IsValidProfileId("Work"));
  EXPECT_FALSE(IsValidProfileId(".."));
  EXPECT_FALSE(IsValidProfileId("a/b"));
  EXPECT_FALSE(IsValidProfileId(std::string(33, 'a')));
}

TEST_F(ProfileRegistryTest, FirstStartCreatesTheFirstProfile) {
  ProfileRegistry registry(dir_);
  registry.Load();
  ASSERT_EQ(registry.profiles().size(), 1u);
  EXPECT_EQ(registry.default_profile().id, "default");
  EXPECT_EQ(registry.default_profile().name, "Main");
  EXPECT_TRUE(
      std::filesystem::exists(dir_ / "profiles" / "default" / "settings.yaml"));
  EXPECT_TRUE(registry.problems().empty());
}

TEST_F(ProfileRegistryTest, OldEmptyDefaultFolderGetsItsName) {
  std::filesystem::create_directories(dir_ / "profiles" / "default");
  ProfileRegistry registry(dir_);
  registry.Load();
  ASSERT_EQ(registry.profiles().size(), 1u);
  EXPECT_EQ(registry.profiles()[0].name, "Main");
}

TEST_F(ProfileRegistryTest, CreateUpdateAndReload) {
  ProfileRegistry registry(dir_);
  registry.Load();
  Result<ProfileId> work = registry.CreateProfile("Work", "green");
  ASSERT_TRUE(work) << work.error().message;
  EXPECT_TRUE(IsValidProfileId(work.value()));
  EXPECT_FALSE(registry.CreateProfile("work", "red"));  // Same name.
  EXPECT_FALSE(registry.CreateProfile("  ", "red"));
  EXPECT_FALSE(registry.CreateProfile("Other", "magenta"));
  EXPECT_FALSE(registry.UpdateProfile(work.value(), "Client A", "red"));
  EXPECT_FALSE(registry.UpdateProfile("default", "Main", "blue"));
  EXPECT_TRUE(registry.UpdateProfile("default", "client a", "blue"));

  ProfileRegistry again(dir_);
  again.Load();
  ASSERT_EQ(again.profiles().size(), 2u);
  // Sorted by name.
  EXPECT_EQ(again.profiles()[0].name, "Client A");
  EXPECT_EQ(again.profiles()[0].color, "red");
  EXPECT_EQ(again.profiles()[1].id, "default");
  EXPECT_EQ(again.FindByName("CLIENT A")->id, work.value());
}

TEST_F(ProfileRegistryTest, DefaultProfileSetting) {
  ProfileRegistry registry(dir_);
  registry.Load();
  Result<ProfileId> work = registry.CreateProfile("Work", "green");
  ASSERT_TRUE(work);
  EXPECT_FALSE(registry.SetDefaultProfile(work.value()));
  EXPECT_TRUE(registry.SetDefaultProfile("p-missing"));
  EXPECT_EQ(registry.default_profile().id, work.value());

  ProfileRegistry again(dir_);
  again.Load();
  EXPECT_EQ(again.default_profile().id, work.value());
}

TEST_F(ProfileRegistryTest, MissingDefaultFallsBackToFirstProfile) {
  Write("settings.yaml", "profiles:\n  default: p-gone\n");
  ProfileRegistry registry(dir_);
  registry.Load();
  EXPECT_EQ(registry.default_profile().id, "default");
}

TEST_F(ProfileRegistryTest, ProblemsAreCollectedWithTheirFile) {
  Write("settings.yaml", "profiles:\n  show_manager_at_startup: yes\n");
  Write("profiles/p-1/settings.yaml", "profile:\n  color: magenta\n");
  Write("profiles/Bad Name/settings.yaml", "");
  ProfileRegistry registry(dir_);
  registry.Load();
  ASSERT_EQ(registry.problems().size(), 3u);
  EXPECT_EQ(registry.problems()[0].rfind("settings.yaml: line 2", 0), 0u)
      << registry.problems()[0];
  // The broken profile still loads, with the default colour.
  ASSERT_TRUE(registry.Find("p-1"));
  EXPECT_EQ(registry.Find("p-1")->color, "blue");
  EXPECT_FALSE(registry.Find("Bad Name"));
}

TEST_F(ProfileRegistryTest, UnusedColor) {
  ProfileRegistry registry(dir_);
  registry.Load();  // "Main" is blue.
  EXPECT_EQ(registry.UnusedColor(), "grey");
  ASSERT_TRUE(registry.CreateProfile("A", "grey"));
  EXPECT_EQ(registry.UnusedColor(), "red");
}

TEST_F(ProfileRegistryTest, ShowManagerAtStartup) {
  ProfileRegistry registry(dir_);
  registry.Load();
  EXPECT_FALSE(registry.show_manager_at_startup());
  EXPECT_FALSE(registry.SetShowManagerAtStartup(true));
  ProfileRegistry again(dir_);
  again.Load();
  EXPECT_TRUE(again.show_manager_at_startup());
}

TEST_F(ProfileRegistryTest, DeleteProfile) {
  ProfileRegistry registry(dir_);
  registry.Load();
  EXPECT_TRUE(registry.DeleteProfile("default"));  // Default and last.
  Result<ProfileId> work = registry.CreateProfile("Work", "green");
  ASSERT_TRUE(work);
  EXPECT_TRUE(registry.DeleteProfile("default"));  // Still the default.
  EXPECT_TRUE(registry.DeleteProfile("p-missing"));
  EXPECT_FALSE(registry.DeleteProfile(work.value()));
  EXPECT_FALSE(registry.Find(work.value()));
  EXPECT_FALSE(std::filesystem::exists(dir_ / "profiles" / work.value()));
  ProfileRegistry again(dir_);
  again.Load();
  EXPECT_EQ(again.profiles().size(), 1u);
}

TEST_F(ProfileRegistryTest, RemovesWebDataOfDeletedProfilesOnly) {
  ProfileRegistry registry(dir_);
  registry.Load();
  Result<ProfileId> work = registry.CreateProfile("Work", "green");
  ASSERT_TRUE(work);
  std::filesystem::path web = dir_ / "web";
  for (const char* name :
       {"default", "p-0000abcd", "Crashpad", "component_crx_cache", "p-1"}) {
    std::filesystem::create_directories(web / name);
  }
  std::filesystem::create_directories(web / work.value());
  Write("web/p-0000abcd/Cookies", "x");
  EXPECT_EQ(registry.RemoveWebDataOfDeletedProfiles(web), 1);
  EXPECT_FALSE(std::filesystem::exists(web / "p-0000abcd"));
  for (const std::string& kept :
       {std::string("default"), work.value(), std::string("Crashpad"),
        std::string("component_crx_cache"), std::string("p-1")}) {
    EXPECT_TRUE(std::filesystem::exists(web / kept)) << kept;
  }
}

}  // namespace
}  // namespace brouk
