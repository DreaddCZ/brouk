#include "core/profile_colors.h"

#include <gtest/gtest.h>

#include <string_view>

#include "core/settings_schema.h"

namespace brouk {
namespace {

TEST(ProfileColorsTest, EveryProfileColorHasItsOwnShades) {
  const ProfileColorShades grey = ShadesOfProfileColor("grey");
  for (std::string_view name : ProfileColorNames()) {
    const ProfileColorShades shades = ShadesOfProfileColor(name);
    if (name != "grey") {
      EXPECT_NE(shades.light, grey.light) << name;
    }
  }
}

TEST(ProfileColorsTest, KnownValuesAndTextColour) {
  EXPECT_EQ(ShadesOfProfileColor("blue").light, 0x1A73E8u);
  EXPECT_EQ(ShadesOfProfileColor("blue").dark, 0x8AB4F8u);
  EXPECT_TRUE(ShadesOfProfileColor("blue").light_text_on_fill);
  EXPECT_FALSE(ShadesOfProfileColor("yellow").light_text_on_fill);
}

TEST(ProfileColorsTest, UnknownNamesAreGrey) {
  EXPECT_EQ(ShadesOfProfileColor("chartreuse").light,
            ShadesOfProfileColor("grey").light);
}

}  // namespace
}  // namespace brouk
