#include "core/download_paths.h"

#include <set>
#include <string>

#include <gtest/gtest.h>

namespace brouk {
namespace {

TEST(DownloadPathsTest, SanitizeKeepsNormalNames) {
  EXPECT_EQ(SanitizeDownloadFileName("report.pdf"), "report.pdf");
  EXPECT_EQ(SanitizeDownloadFileName("Účtenka č. 5.pdf"), "Účtenka č. 5.pdf");
}

TEST(DownloadPathsTest, SanitizeStripsDirectories) {
  EXPECT_EQ(SanitizeDownloadFileName("../../etc/passwd"), "passwd");
  EXPECT_EQ(SanitizeDownloadFileName("a\\b\\c.txt"), "c.txt");
  EXPECT_EQ(SanitizeDownloadFileName("/"), "download");
}

TEST(DownloadPathsTest, SanitizeRemovesHiddenAndControlCharacters) {
  EXPECT_EQ(SanitizeDownloadFileName(".bashrc"), "bashrc");
  EXPECT_EQ(SanitizeDownloadFileName(".."), "download");
  EXPECT_EQ(SanitizeDownloadFileName(""), "download");
  EXPECT_EQ(SanitizeDownloadFileName("  name.txt.  "), "name.txt");
  EXPECT_EQ(SanitizeDownloadFileName("a\nb\x01:c.txt"), "a_b__c.txt");
}

TEST(DownloadPathsTest, SanitizeShortensLongNamesKeepingExtension) {
  std::string long_name = std::string(300, 'x') + ".pdf";
  std::string result = SanitizeDownloadFileName(long_name);
  EXPECT_EQ(result.size(), 255u);
  EXPECT_TRUE(result.ends_with(".pdf"));
  // Multi-byte characters are not cut in half.
  std::string czech;
  for (int i = 0; i < 200; ++i) {
    czech += "ž";  // 2 bytes each.
  }
  std::string shortened = SanitizeDownloadFileName(czech + ".txt");
  EXPECT_LE(shortened.size(), 255u);
  EXPECT_EQ((shortened.size() - 4) % 2, 0u);
}

TEST(DownloadPathsTest, NumberedNames) {
  EXPECT_EQ(NumberedFileName("report.pdf", 1), "report (1).pdf");
  EXPECT_EQ(NumberedFileName("archive.tar.gz", 2), "archive (2).tar.gz");
  EXPECT_EQ(NumberedFileName("README", 3), "README (3)");
  EXPECT_EQ(NumberedFileName("v1.2.zip", 1), "v1.2 (1).zip");
}

TEST(DownloadPathsTest, ReserveNumbersExistingAndReservedFiles) {
  std::set<std::string> existing = {"/dl/a.txt", "/dl/a (1).txt"};
  DownloadPathReserver reserver(
      [&existing](const std::string& path) { return existing.contains(path); });
  EXPECT_EQ(reserver.Reserve("/dl", "a.txt"), "/dl/a (2).txt");
  // The same name again while the first download runs.
  EXPECT_EQ(reserver.Reserve("/dl/", "a.txt"), "/dl/a (3).txt");
  EXPECT_EQ(reserver.Reserve("/dl", "b.txt"), "/dl/b.txt");
  reserver.Release("/dl/a (2).txt");
  EXPECT_EQ(reserver.Reserve("/dl", "a.txt"), "/dl/a (2).txt");
}

TEST(DownloadPathsTest, ReserveSanitizes) {
  DownloadPathReserver reserver([](const std::string&) { return false; });
  EXPECT_EQ(reserver.Reserve("/dl", "../../x.sh"), "/dl/x.sh");
}

}  // namespace
}  // namespace brouk
