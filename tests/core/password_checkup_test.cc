#include "core/password_checkup.h"

#include <gtest/gtest.h>

namespace brouk {
namespace {

TEST(PasswordCheckupTest, QuerySendsOnlyPrefix) {
  // SHA-1 of "password" is 5BAA61E4C9B93F3F0682250B6CF8331B7EE68FD8.
  PwnedQuery query = MakePwnedQuery("password");
  EXPECT_EQ(query.prefix, "5BAA6");
  EXPECT_EQ(query.suffix, "1E4C9B93F3F0682250B6CF8331B7EE68FD8");
}

TEST(PasswordCheckupTest, CountsMatchesInResponse) {
  std::string response =
      "003D68EB55068C33ACE09247EE4C639306B:3\r\n"
      "1E4C9B93F3F0682250B6CF8331B7EE68FD8:9545824\r\n"
      "1e4c9b93f3f0682250b6cf8331b7ee68fd9:0\r\n";
  EXPECT_EQ(PwnedCount(response, "1E4C9B93F3F0682250B6CF8331B7EE68FD8"),
            9545824);
  // Padding lines count 0; unknown suffixes too.
  EXPECT_EQ(PwnedCount(response, "1E4C9B93F3F0682250B6CF8331B7EE68FD9"), 0);
  EXPECT_EQ(PwnedCount(response, "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"), 0);
  EXPECT_EQ(PwnedCount("", "1E4C"), 0);
}

TEST(PasswordCheckupTest, FindsReusedPasswords) {
  std::vector<std::string> passwords = {"a", "b", "a", "c", "b", "a", ""};
  auto groups = FindReusedPasswords(passwords);
  ASSERT_EQ(groups.size(), 2U);
  EXPECT_EQ(groups[0], (std::vector<std::size_t>{0, 2, 5}));
  EXPECT_EQ(groups[1], (std::vector<std::size_t>{1, 4}));
  EXPECT_TRUE(FindReusedPasswords(std::vector<std::string>{"", ""}).empty());
}

}  // namespace
}  // namespace brouk
