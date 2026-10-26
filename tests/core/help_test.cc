#include "core/help.h"

#include <string>

#include <gtest/gtest.h>

namespace brouk {
namespace {

TEST(HelpTest, AnchorsAreLowercaseWordsWithDashes) {
  EXPECT_TRUE(IsHelpAnchor("history-search"));
  EXPECT_TRUE(IsHelpAnchor("settings-secure-dns"));
  EXPECT_TRUE(IsHelpAnchor("a"));
  EXPECT_FALSE(IsHelpAnchor(""));
  EXPECT_FALSE(IsHelpAnchor("History"));
  EXPECT_FALSE(IsHelpAnchor("a b"));
  EXPECT_FALSE(IsHelpAnchor("x\"); alert(1); (\""));
  EXPECT_FALSE(IsHelpAnchor("#history"));
  EXPECT_FALSE(IsHelpAnchor(std::string(65, 'a')));
}

}  // namespace
}  // namespace brouk
