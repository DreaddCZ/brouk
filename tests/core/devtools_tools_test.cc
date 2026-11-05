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
  // The security check reads the page's elements (029), never changes them.
  EXPECT_TRUE(IsAllowedToolsProtocolMethod("DOM.querySelectorAll"));
  EXPECT_FALSE(IsAllowedToolsProtocolMethod("DOM.setAttributeValue"));
  EXPECT_FALSE(IsAllowedToolsProtocolMethod("DOM.setOuterHTML"));
  EXPECT_FALSE(IsAllowedToolsProtocolMethod("DOM.resolveNode"));
  // Nothing that runs code in the page, navigates or reaches other targets.
  EXPECT_FALSE(IsAllowedToolsProtocolMethod("Runtime.evaluate"));
  EXPECT_FALSE(IsAllowedToolsProtocolMethod("Page.navigate"));
  EXPECT_FALSE(IsAllowedToolsProtocolMethod("Target.createTarget"));
  EXPECT_FALSE(IsAllowedToolsProtocolMethod("Network.getAllCookies"));
  EXPECT_FALSE(IsAllowedToolsProtocolMethod("network.getcookies"));
  EXPECT_FALSE(IsAllowedToolsProtocolMethod(""));
}

TEST(DevToolsToolsTest, OpensOnlyOwaspLinks) {
  EXPECT_TRUE(
      ToolsPageOpensLink("https://cheatsheetseries.owasp.org/cheatsheets/"
                         "Content_Security_Policy_Cheat_Sheet.html"));
  EXPECT_TRUE(ToolsPageOpensLink("https://owasp.org/www-community/x"));
  EXPECT_FALSE(ToolsPageOpensLink("https://owasp.org.example.com/"));
  EXPECT_FALSE(ToolsPageOpensLink("http://owasp.org/"));
  EXPECT_FALSE(ToolsPageOpensLink("https://example.com/"));
  EXPECT_FALSE(ToolsPageOpensLink("javascript:alert(1)"));
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
