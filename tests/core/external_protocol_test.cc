#include "core/external_protocol.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>

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

TEST(ExternalProtocolTest, ShellUrlEscapesWhatCouldSplitTheCommandLine) {
  EXPECT_EQ(ExternalUrlForShell("mailto:a@example.com?subject=Hi there"),
            "mailto:a@example.com?subject=Hi%20there");
  EXPECT_EQ(ExternalUrlForShell("x-app:\"a\" b|c<d>"),
            "x-app:%22a%22%20b%7Cc%3Cd%3E");
  EXPECT_EQ(ExternalUrlForShell("x-app:\\x^`{}"), "x-app:%5Cx%5E%60%7B%7D");
  EXPECT_EQ(ExternalUrlForShell("x-app:a\tb\nc"), "x-app:a%09b%0Ac");
  // UTF-8 bytes ("č"); escapes already in the URL stay as they are.
  EXPECT_EQ(ExternalUrlForShell("x-app:\xC4\x8D%20"), "x-app:%C4%8D%20");
  EXPECT_EQ(ExternalUrlForShell("msteams:/l/chat/0/0?users=a@example.com"),
            "msteams:/l/chat/0/0?users=a@example.com");
}

TEST(ExternalProtocolTest, ShellUrlRejectsDrivesAndLongUrls) {
  EXPECT_EQ(ExternalUrlForShell("c:/Windows/System32/calc.exe"), std::nullopt);
  EXPECT_EQ(ExternalUrlForShell("C:\\x"), std::nullopt);
  EXPECT_EQ(ExternalUrlForShell("no scheme"), std::nullopt);
  EXPECT_EQ(ExternalUrlForShell(""), std::nullopt);
  const std::string longest = "x-app:" + std::string(2048 - 6, 'a');
  EXPECT_EQ(ExternalUrlForShell(longest), longest);
  EXPECT_EQ(ExternalUrlForShell(longest + "a"), std::nullopt);
  // Escaping counts: with 681 spaces, 2049 characters.
  EXPECT_EQ(ExternalUrlForShell("x-app:" + std::string(681, ' ')),
            std::nullopt);
}

}  // namespace
}  // namespace brouk
