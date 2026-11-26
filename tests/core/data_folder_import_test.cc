#include "core/data_folder_import.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

#include "fake_secret_store.h"

namespace brouk {
namespace {

class DataFolderImportTest : public testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("brouk-data-import-test-" +
            std::string(
                testing::UnitTest::GetInstance()->current_test_info()->name()));
    std::error_code error;
    std::filesystem::remove_all(dir_, error);
    std::filesystem::create_directories(dir_);
  }
  void TearDown() override {
    std::error_code error;
    std::filesystem::remove_all(dir_, error);
  }

  static void WriteFile(const std::filesystem::path& path,
                        const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << text;
  }
  static std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};
  }

  // A data folder as the direct download leaves it.
  std::filesystem::path MakeSource() {
    const std::filesystem::path from = dir_ / "from";
    WriteFile(from / "settings.yaml", "startup: restore\n");
    WriteFile(from / "profiles" / "work" / "settings.yaml", "name: Work\n");
    WriteFile(from / "profiles" / "work" / "passwords.db", "secret rows");
    WriteFile(from / "web" / "work" / "Cookies", "cookies");
    WriteFile(from / "web" / "work" / "Local Storage" / "leveldb" / "LOCK", "");
    WriteFile(from / "web" / "work" / "Cache" / "Cache_Data" / "data_0", "c");
    WriteFile(from / "web" / "work" / "Code Cache" / "js" / "index", "c");
    WriteFile(from / "web" / "GrShaderCache" / "data_0", "c");
    WriteFile(from / "web" / "lockfile", "");
    WriteFile(from / "web" / "Local State", "{}");
    WriteFile(from / "logs" / "brouk.log", "log");
    WriteFile(from / "updates" / "Brouk-setup.exe", "exe");
    return from;
  }

  std::filesystem::path dir_;
};

TEST(DataImportRequestTest, ParsesOnlyCopyAndMove) {
  EXPECT_EQ(ParseDataImportRequest("copy"), DataImportMode::kCopy);
  EXPECT_EQ(ParseDataImportRequest(" move\r\n"), DataImportMode::kMove);
  EXPECT_EQ(ParseDataImportRequest(""), std::nullopt);
  EXPECT_EQ(ParseDataImportRequest("Move"), std::nullopt);
  EXPECT_EQ(ParseDataImportRequest("copy move"), std::nullopt);
  for (DataImportMode mode : {DataImportMode::kCopy, DataImportMode::kMove}) {
    EXPECT_EQ(ParseDataImportRequest(DataImportRequestText(mode)), mode);
  }
}

TEST(DataImportRequestTest, SkipsCachesLogsAndUpdatesOnly) {
  EXPECT_TRUE(SkippedByDataImport("logs"));
  EXPECT_TRUE(SkippedByDataImport("updates"));
  EXPECT_TRUE(SkippedByDataImport("web/work/Cache"));
  EXPECT_TRUE(SkippedByDataImport("web/work/Code Cache"));
  EXPECT_TRUE(SkippedByDataImport("web/work/GPUCache"));
  EXPECT_TRUE(SkippedByDataImport("web/GrShaderCache"));
  EXPECT_TRUE(SkippedByDataImport("web/lockfile"));
  EXPECT_FALSE(SkippedByDataImport("settings.yaml"));
  EXPECT_FALSE(SkippedByDataImport("profiles/logs"));
  EXPECT_FALSE(SkippedByDataImport("profiles/work/Cache"));
  EXPECT_FALSE(SkippedByDataImport("web/work/Cookies"));
  EXPECT_FALSE(SkippedByDataImport("web/work/Local Storage"));
  EXPECT_FALSE(SkippedByDataImport("web"));
}

TEST_F(DataFolderImportTest, CopiesDataWithoutCaches) {
  const std::filesystem::path from = MakeSource();
  const std::filesystem::path to = dir_ / "to";
  ASSERT_EQ(CopyDataFolder(from, to), std::nullopt);
  EXPECT_EQ(ReadFile(to / "settings.yaml"), "startup: restore\n");
  EXPECT_EQ(ReadFile(to / "profiles" / "work" / "passwords.db"), "secret rows");
  EXPECT_EQ(ReadFile(to / "web" / "work" / "Cookies"), "cookies");
  EXPECT_TRUE(std::filesystem::exists(to / "web" / "work" / "Local Storage" /
                                      "leveldb" / "LOCK"));
  EXPECT_EQ(ReadFile(to / "web" / "Local State"), "{}");
  EXPECT_FALSE(std::filesystem::exists(to / "web" / "work" / "Cache"));
  EXPECT_FALSE(std::filesystem::exists(to / "web" / "work" / "Code Cache"));
  EXPECT_FALSE(std::filesystem::exists(to / "web" / "GrShaderCache"));
  EXPECT_FALSE(std::filesystem::exists(to / "web" / "lockfile"));
  EXPECT_FALSE(std::filesystem::exists(to / "logs"));
  EXPECT_FALSE(std::filesystem::exists(to / "updates"));
  // The source is untouched.
  EXPECT_TRUE(std::filesystem::exists(from / "logs" / "brouk.log"));
}

TEST_F(DataFolderImportTest, CopyRefusesMissingSourceOrExistingTarget) {
  EXPECT_NE(CopyDataFolder(dir_ / "missing", dir_ / "to"), std::nullopt);
  EXPECT_FALSE(std::filesystem::exists(dir_ / "to"));
  const std::filesystem::path from = MakeSource();
  WriteFile(dir_ / "to" / "keep", "mine");
  EXPECT_NE(CopyDataFolder(from, dir_ / "to"), std::nullopt);
  EXPECT_EQ(ReadFile(dir_ / "to" / "keep"), "mine");
}

TEST_F(DataFolderImportTest, ReplaceSwapsTheWholeFolder) {
  const std::filesystem::path from = MakeSource();
  const std::filesystem::path to = dir_ / "Brouk";
  WriteFile(to / "settings.yaml", "old\n");
  WriteFile(to / "profiles" / "old" / "settings.yaml", "name: Old\n");
  // Left over from an import that was cut short.
  WriteFile(dir_ / "Brouk.import" / "half", "x");
  ASSERT_EQ(ReplaceDataFolder(from, to), std::nullopt);
  EXPECT_EQ(ReadFile(to / "settings.yaml"), "startup: restore\n");
  EXPECT_FALSE(std::filesystem::exists(to / "profiles" / "old"));
  EXPECT_FALSE(std::filesystem::exists(to / "half"));
  EXPECT_FALSE(std::filesystem::exists(dir_ / "Brouk.import"));
}

TEST_F(DataFolderImportTest, FailedReplaceKeepsTheOldFolder) {
  const std::filesystem::path to = dir_ / "Brouk";
  WriteFile(to / "settings.yaml", "old\n");
  EXPECT_NE(ReplaceDataFolder(dir_ / "missing", to), std::nullopt);
  EXPECT_EQ(ReadFile(to / "settings.yaml"), "old\n");
  EXPECT_FALSE(std::filesystem::exists(dir_ / "Brouk.import"));
}

TEST_F(DataFolderImportTest, CopiesTheKeysOfEveryProfile) {
  const std::filesystem::path from = MakeSource();
  WriteFile(from / "profiles" / "home" / "settings.yaml", "name: Home\n");
  FakeSecretStore source;
  source.items[PasswordKeyAccount("work")] = "work key";
  source.items[std::string(kAutofillKeyAccount)] = "autofill key";
  source.items["bitwarden-session-u1"] = "session";
  FakeSecretStore target;
  ASSERT_EQ(CopyDataKeys(from, source, target, false), std::nullopt);
  EXPECT_EQ(target.items[PasswordKeyAccount("work")], "work key");
  EXPECT_EQ(target.items[std::string(kAutofillKeyAccount)], "autofill key");
  // "home" has no key yet; Bitwarden sessions stay behind.
  EXPECT_FALSE(target.items.contains(PasswordKeyAccount("home")));
  EXPECT_FALSE(target.items.contains("bitwarden-session-u1"));
  EXPECT_EQ(source.items.size(), 3U);
}

TEST_F(DataFolderImportTest, MovingKeysRemovesThemFromTheSource) {
  const std::filesystem::path from = MakeSource();
  FakeSecretStore source;
  source.items[PasswordKeyAccount("work")] = "work key";
  source.items["bitwarden-session-u1"] = "session";
  FakeSecretStore target;
  ASSERT_EQ(CopyDataKeys(from, source, target, true), std::nullopt);
  EXPECT_EQ(target.items[PasswordKeyAccount("work")], "work key");
  EXPECT_FALSE(source.items.contains(PasswordKeyAccount("work")));
  EXPECT_TRUE(source.items.contains("bitwarden-session-u1"));
}

TEST_F(DataFolderImportTest, KeychainFailureStopsTheCopy) {
  const std::filesystem::path from = MakeSource();
  FakeSecretStore source;
  source.items[PasswordKeyAccount("work")] = "work key";
  source.fail = true;
  FakeSecretStore target;
  EXPECT_NE(CopyDataKeys(from, source, target, true), std::nullopt);
  source.fail = false;
  EXPECT_TRUE(source.items.contains(PasswordKeyAccount("work")));
}

}  // namespace
}  // namespace brouk
