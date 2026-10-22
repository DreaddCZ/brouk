#include "core/external_protocol.h"

#include <gtest/gtest.h>

namespace brouk {
namespace {

TEST(ExternalProtocolTest, WebSchemesStayInTheBrowser) {
  EXPECT_EQ(ClassifyScheme("https://example.com"), SchemeHandling::kBrowser);
  EXPECT_EQ(ClassifyScheme("HTTP://example.com"), SchemeHandling::kBrowser);
  EXPECT_EQ(ClassifyScheme("data:text/plain,x"), SchemeHandling::kBrowser);
  EXPECT_EQ(ClassifyScheme("about:blank"), SchemeHandling::kBrowser);
  EXPECT_EQ(ClassifyScheme("blob:https://a/b"), SchemeHandling::kBrowser);
  EXPECT_EQ(ClassifyScheme("javascript:void(0)"), SchemeHandling::kBrowser);
}

TEST(ExternalProtocolTest, AppSchemesAreExternal) {
  EXPECT_EQ(ClassifyScheme("mailto:a@b.c"), SchemeHandling::kExternal);
  EXPECT_EQ(ClassifyScheme("tel:+420123"), SchemeHandling::kExternal);
  EXPECT_EQ(ClassifyScheme("msteams:/l/chat"), SchemeHandling::kExternal);
  EXPECT_EQ(ClassifyScheme("zoommtg://zoom.us/join"),
            SchemeHandling::kExternal);
  EXPECT_EQ(ClassifyScheme("Slack://open"), SchemeHandling::kExternal);
}

TEST(ExternalProtocolTest, DangerousSchemesAreBlocked) {
  EXPECT_EQ(ClassifyScheme("shell:startup"), SchemeHandling::kBlocked);
  EXPECT_EQ(ClassifyScheme("vbscript:msgbox"), SchemeHandling::kBlocked);
  EXPECT_EQ(ClassifyScheme("ms-help://x"), SchemeHandling::kBlocked);
  // Internal pages never load in web tabs.
  EXPECT_EQ(ClassifyScheme("brouk://profiles/"), SchemeHandling::kBlocked);
}

TEST(ExternalProtocolTest, InvalidSchemesAreLeftToTheBrowser) {
  EXPECT_EQ(ClassifyScheme(""), SchemeHandling::kBrowser);
  EXPECT_EQ(ClassifyScheme("no scheme here"), SchemeHandling::kBrowser);
  EXPECT_EQ(ClassifyScheme("1abc:x"), SchemeHandling::kBrowser);
  EXPECT_EQ(SchemeOf("MailTo:x"), "mailto");
  EXPECT_EQ(SchemeOf(":x"), "");
}

TEST(ExternalProtocolTest, AllowKey) {
  EXPECT_EQ(ExternalProtocolAllowKey("https://teams.microsoft.com", "msteams"),
            "https://teams.microsoft.com msteams");
}

}  // namespace
}  // namespace brouk
