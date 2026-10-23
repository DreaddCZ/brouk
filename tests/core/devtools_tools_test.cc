#include "core/devtools_tools.h"

#include <gtest/gtest.h>

namespace brouk {
namespace {

TEST(DevToolsToolsTest, OnlyListedMethods) {
  EXPECT_TRUE(IsAllowedToolsProtocolMethod("Network.getCookies"));
  EXPECT_TRUE(IsAllowedToolsProtocolMethod("Storage.clearDataForOrigin"));
  // Nothing that runs code in the page, navigates or reaches other targets.
  EXPECT_FALSE(IsAllowedToolsProtocolMethod("Runtime.evaluate"));
  EXPECT_FALSE(IsAllowedToolsProtocolMethod("Page.navigate"));
  EXPECT_FALSE(IsAllowedToolsProtocolMethod("Target.createTarget"));
  EXPECT_FALSE(IsAllowedToolsProtocolMethod("Network.getAllCookies"));
  EXPECT_FALSE(IsAllowedToolsProtocolMethod("network.getcookies"));
  EXPECT_FALSE(IsAllowedToolsProtocolMethod(""));
}

}  // namespace
}  // namespace brouk
