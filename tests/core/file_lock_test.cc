#include "core/file_lock.h"

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include <gtest/gtest.h>

namespace brouk {
namespace {

class FileLockTest : public testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("brouk-file-lock-test-" +
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

  std::filesystem::path dir_;
};

TEST_F(FileLockTest, OnlyOneHolderAtATime) {
  const std::filesystem::path path = dir_ / "chromium.lock";
  std::optional<FileLock> first = FileLock::TryLock(path);
  ASSERT_TRUE(first);
  EXPECT_TRUE(std::filesystem::exists(path));
  EXPECT_FALSE(FileLock::TryLock(path));
  first.reset();
  EXPECT_TRUE(FileLock::TryLock(path));
}

TEST_F(FileLockTest, MovingKeepsTheLock) {
  const std::filesystem::path path = dir_ / "chromium.lock";
  std::optional<FileLock> first = FileLock::TryLock(path);
  ASSERT_TRUE(first);
  std::optional<FileLock> moved = std::move(first);  // first stays set.
  first.reset();
  EXPECT_FALSE(FileLock::TryLock(path));
  moved.reset();
  EXPECT_TRUE(FileLock::TryLock(path));
}

TEST_F(FileLockTest, AMissingFolderIsNoLock) {
  EXPECT_FALSE(FileLock::TryLock(dir_ / "missing" / "chromium.lock"));
}

}  // namespace
}  // namespace brouk
