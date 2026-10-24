#include "core/password_generator.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>

namespace brouk {
namespace {

bool Has(const std::string& text, int (*test)(int)) {
  return std::ranges::any_of(
      text, [&](char c) { return test(static_cast<unsigned char>(c)) != 0; });
}

TEST(PasswordGeneratorTest, DefaultHasEveryKind) {
  for (int i = 0; i < 50; ++i) {
    std::string password = GeneratePassword({});
    EXPECT_EQ(password.size(), 20U);
    EXPECT_TRUE(Has(password, std::isupper));
    EXPECT_TRUE(Has(password, std::islower));
    EXPECT_TRUE(Has(password, std::isdigit));
    EXPECT_TRUE(Has(password, std::ispunct));
    EXPECT_EQ(password.find_first_of("lIO01"), std::string::npos);
  }
  EXPECT_NE(GeneratePassword({}), GeneratePassword({}));
}

TEST(PasswordGeneratorTest, FollowsRules) {
  PasswordRules rules;
  rules.length = 12;
  rules.symbols = false;
  rules.upper = false;
  std::string password = GeneratePassword(rules);
  EXPECT_EQ(password.size(), 12U);
  EXPECT_FALSE(Has(password, std::isupper));
  EXPECT_FALSE(Has(password, std::ispunct));
  rules.length = 2;
  EXPECT_EQ(GeneratePassword(rules).size(), 8U);
  rules.length = 500;
  EXPECT_EQ(GeneratePassword(rules).size(), 64U);
  PasswordRules none{16, false, false, false, false};
  std::string fallback = GeneratePassword(none);
  EXPECT_EQ(fallback.size(), 16U);
  EXPECT_TRUE(std::ranges::all_of(fallback, [](char c) {
    return std::islower(static_cast<unsigned char>(c)) != 0 ||
           std::isdigit(static_cast<unsigned char>(c)) != 0;
  }));
}

}  // namespace
}  // namespace brouk
