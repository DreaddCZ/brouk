#include "core/dialog_spam_guard.h"

#include <gtest/gtest.h>

namespace brouk {
namespace {

TEST(DialogSpamGuardTest, FirstDialogHasNoCheckbox) {
  DialogSpamGuard guard;
  EXPECT_FALSE(guard.IsSuppressed());
  EXPECT_FALSE(guard.ShouldOfferSuppression());
}

TEST(DialogSpamGuardTest, SecondDialogOffersCheckbox) {
  DialogSpamGuard guard;
  guard.OnDialogClosed(false);
  EXPECT_TRUE(guard.ShouldOfferSuppression());
  EXPECT_FALSE(guard.IsSuppressed());
}

TEST(DialogSpamGuardTest, CheckboxSuppressesUntilReset) {
  DialogSpamGuard guard;
  guard.OnDialogClosed(false);
  guard.OnDialogClosed(true);
  EXPECT_TRUE(guard.IsSuppressed());
  guard.Reset();
  EXPECT_FALSE(guard.IsSuppressed());
  EXPECT_FALSE(guard.ShouldOfferSuppression());
}

}  // namespace
}  // namespace brouk
