#include "core/devtools_tools.h"

#include <set>
#include <string_view>

#include <gtest/gtest.h>

#include "core/context_menu.h"

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

TEST(DevToolsToolsTest, UtilitiesHaveUniqueIds) {
  std::set<std::string_view> ids;
  for (const DevToolsUtility& utility : DevToolsUtilities()) {
    EXPECT_FALSE(utility.title.empty());
    EXPECT_TRUE(ids.insert(utility.id).second) << utility.id;
  }
  // "Decode With" opens utilities that exist.
  for (MenuCommand command :
       {MenuCommand::kDecodeBase64, MenuCommand::kDecodeUrl,
        MenuCommand::kDecodeUnixTime, MenuCommand::kDecodeHtml}) {
    EXPECT_TRUE(ids.contains(UtilityForDecodeCommand(command)));
  }
}

}  // namespace
}  // namespace brouk
