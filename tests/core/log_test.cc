#include "core/log.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

#include <gtest/gtest.h>

#include "core/log_file.h"

namespace brouk {
namespace {

class LogTest : public testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("brouk-log-test-" +
            std::string(
                testing::UnitTest::GetInstance()->current_test_info()->name()));
    std::error_code error;
    std::filesystem::remove_all(dir_, error);
    SetLogToStderr(false);
  }
  void TearDown() override {
    StopLogFile();
    SetDebugLogging(false);
    SetLogToStderr(true);
    std::error_code error;
    std::filesystem::remove_all(dir_, error);
  }
  std::string Read(const std::string& name) const {
    std::ifstream file(dir_ / name, std::ios::binary);
    return {std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};
  }

  std::filesystem::path dir_;
};

TEST_F(LogTest, WarningsAndErrorsAreAlwaysWritten) {
  ASSERT_FALSE(StartLogFile(dir_));
  LogWarning() << "Updates: download failed: " << 404;
  LogError() << "Bookmarks not saved";
  LogDebug() << "detail";
  LogInfo() << "information";
  const std::string log = Read("brouk.log");
  EXPECT_NE(log.find("WARNING ["), std::string::npos);
  EXPECT_NE(log.find("log_test.cc:"), std::string::npos);
  EXPECT_NE(log.find("Updates: download failed: 404\n"), std::string::npos);
  EXPECT_NE(log.find("ERROR ["), std::string::npos);
  EXPECT_EQ(log.find("detail"), std::string::npos);
  EXPECT_EQ(log.find("information"), std::string::npos);
}

TEST_F(LogTest, DebugLoggingAddsDetailsUntilSwitchedOff) {
  ASSERT_FALSE(StartLogFile(dir_));
  SetDebugLogging(true);
  LogDebug() << "first detail";
  LogInfo() << "information";
  SetDebugLogging(false);
  LogDebug() << "second detail";
  const std::string log = Read("brouk.log");
  EXPECT_NE(log.find("DEBUG ["), std::string::npos);
  EXPECT_NE(log.find("first detail"), std::string::npos);
  EXPECT_NE(log.find("INFO ["), std::string::npos);
  EXPECT_EQ(log.find("second detail"), std::string::npos);
}

TEST_F(LogTest, ThreadsAreNamed) {
  ASSERT_FALSE(StartLogFile(dir_));
  SetLogThreadNamer([] { return std::string("UI"); });
  LogWarning() << "named";
  SetLogThreadNamer(nullptr);
  EXPECT_NE(Read("brouk.log").find("WARNING [UI] "), std::string::npos);
}

TEST_F(LogTest, LineBreaksStayInOneLine) {
  const std::string line =
      FormatLogLine(std::chrono::system_clock::now(), LogLevel::kWarning, "UI",
                    std::source_location::current(), "one\r\ntwo\nthree");
  EXPECT_EQ(line.find('\n'), std::string::npos);
  EXPECT_NE(line.find("one⏎two⏎three"), std::string::npos);
}

TEST(RotatingLogFileTest, KeepsAtMostThreeFilesOfTheMaximumSize) {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "brouk-log-rotation-test";
  std::error_code error;
  std::filesystem::remove_all(dir, error);
  {
    Result<RotatingLogFile> log =
        RotatingLogFile::Open(dir, "brouk", {.max_bytes = 100, .max_files = 3});
    ASSERT_TRUE(log);
    const std::string line(39, 'x');  // 40 bytes with the line break.
    for (int i = 0; i < 20; ++i) {
      log.value().Write(line);
    }
  }
  int files = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    ++files;
    EXPECT_LE(entry.file_size(), 100u) << entry.path();
  }
  EXPECT_EQ(files, 3);
  EXPECT_TRUE(std::filesystem::exists(dir / "brouk.log"));
  EXPECT_TRUE(std::filesystem::exists(dir / "brouk.1.log"));
  EXPECT_TRUE(std::filesystem::exists(dir / "brouk.2.log"));
  std::filesystem::remove_all(dir, error);
}

TEST(RotatingLogFileTest, AppendsToAnExistingLog) {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "brouk-log-append-test";
  std::error_code error;
  std::filesystem::remove_all(dir, error);
  for (const char* text : {"first", "second"}) {
    Result<RotatingLogFile> log = RotatingLogFile::Open(dir, "brouk");
    ASSERT_TRUE(log);
    log.value().Write(text);
  }
  std::ifstream file(dir / "brouk.log", std::ios::binary);
  const std::string text{std::istreambuf_iterator<char>(file),
                         std::istreambuf_iterator<char>()};
  EXPECT_EQ(text, "first\nsecond\n");
  file.close();
  std::filesystem::remove_all(dir, error);
}

TEST(UrlForLogTest, DropsQueryFragmentAndCredentials) {
  EXPECT_EQ(UrlForLog("https://example.com/login?pw=secret#token"),
            "https://example.com/login");
  EXPECT_EQ(UrlForLog("https://user:secret@example.com/a/b"),
            "https://example.com/a/b");
  EXPECT_EQ(UrlForLog("https://example.com/a@b"), "https://example.com/a@b");
  EXPECT_EQ(UrlForLog("https://example.com#x"), "https://example.com");
  EXPECT_EQ(UrlForLog("about:blank"), "about:blank");
}

}  // namespace
}  // namespace brouk
