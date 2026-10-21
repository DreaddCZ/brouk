#include "core/permissions.h"

#include <vector>

#include <gtest/gtest.h>

namespace brouk {
namespace {

using K = PermissionKind;
using C = PermissionChoice;

std::vector<C> Choices(const std::vector<PermissionButton>& buttons) {
  std::vector<C> out;
  for (const PermissionButton& b : buttons) {
    out.push_back(b.choice);
  }
  return out;
}

TEST(PermissionsTest, CameraAndMicOfferThreeChoices) {
  std::vector<PermissionButton> buttons =
      PermissionButtons({K::kCamera, K::kMicrophone});
  EXPECT_EQ(Choices(buttons),
            (std::vector<C>{C::kAllow, C::kAllowOnce, C::kBlock}));
  EXPECT_EQ(buttons[0].title, "Allow while visiting the site");
  EXPECT_EQ(buttons[1].title, "Allow this time");
  EXPECT_EQ(buttons[2].title, "Never allow");
}

TEST(PermissionsTest, LocationOffersAllowOnce) {
  EXPECT_TRUE(OffersAllowOnce({K::kLocation}));
}

TEST(PermissionsTest, NotificationsOfferAllowOrBlock) {
  std::vector<PermissionButton> buttons =
      PermissionButtons({K::kNotifications});
  EXPECT_EQ(Choices(buttons), (std::vector<C>{C::kAllow, C::kBlock}));
  EXPECT_EQ(buttons[0].title, "Allow");
  EXPECT_EQ(buttons[1].title, "Block");
}

TEST(PermissionsTest, MixedRequestWithoutOneTimeKind) {
  EXPECT_FALSE(OffersAllowOnce({K::kCamera, K::kNotifications}));
  EXPECT_FALSE(OffersAllowOnce({}));
}

TEST(PermissionsTest, ScreenShareIsNeverRemembered) {
  EXPECT_FALSE(IsRemembered({K::kScreenShare}));
  EXPECT_FALSE(IsRemembered({K::kScreenShare, K::kMicrophone}));
  EXPECT_TRUE(IsRemembered({K::kMicrophone}));
  EXPECT_EQ(Choices(PermissionButtons({K::kScreenShare})),
            (std::vector<C>{C::kAllowOnce, C::kDismiss}));
}

TEST(PermissionsTest, Texts) {
  EXPECT_EQ(PermissionPromptTitle("meet.google.com"),
            "meet.google.com wants to");
  EXPECT_EQ(PermissionDescription(K::kCamera), "Use your camera");
  EXPECT_EQ(PermissionName(K::kMicrophone), "Microphone");
}

}  // namespace
}  // namespace brouk
