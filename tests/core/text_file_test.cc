#include "core/text_file.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <gtest/gtest.h>

namespace brouk {
namespace {

class TextFileTest : public testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("brouk-text-file-test-" +
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
  std::filesystem::path path() const { return dir_ / "file.txt"; }

  std::filesystem::path dir_;
};

TEST_F(TextFileTest, WrittenTextReadsBack) {
  using std::string_literals::operator""s;
  const std::string text = "line one\nline two\r\n\0binary"s;
  EXPECT_FALSE(WriteTextFileAtomically(path(), text));
  Result<std::string> read = ReadTextFile(path(), 1024);
  ASSERT_TRUE(read);
  EXPECT_EQ(read.value(), text);
}

TEST_F(TextFileTest, WritingReplacesTheFileAndLeavesNoTemporaryFile) {
  std::ofstream(path()) << "old and longer text";
  EXPECT_FALSE(WriteTextFileAtomically(path(), "new"));
  EXPECT_EQ(ReadTextFile(path(), 1024).value(), "new");
  EXPECT_FALSE(std::filesystem::exists(dir_ / "file.txt.tmp"));
}

TEST_F(TextFileTest, WritingIntoAMissingFolderFails) {
  EXPECT_TRUE(WriteTextFileAtomically(dir_ / "missing" / "file.txt", "x"));
}

TEST_F(TextFileTest, AMissingFileIsAnError) {
  EXPECT_FALSE(ReadTextFile(path(), 1024));
}

TEST_F(TextFileTest, ATooBigFileIsRefused) {
  std::ofstream(path()) << "12345";
  EXPECT_FALSE(ReadTextFile(path(), 4));
  EXPECT_TRUE(ReadTextFile(path(), 5));
}

TEST_F(TextFileTest, AnEmptyFileReadsAsEmptyText) {
  std::ofstream(path()) << "";
  Result<std::string> read = ReadTextFile(path(), 0);
  ASSERT_TRUE(read);
  EXPECT_TRUE(read.value().empty());
}

}  // namespace
}  // namespace brouk
