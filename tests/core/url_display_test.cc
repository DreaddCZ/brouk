#include "core/url_display.h"

#include <gtest/gtest.h>

namespace brouk {
namespace {

TEST(UrlDisplayTest, HidesHttpSchemesAndWww) {
  EXPECT_EQ(FormatUrlForDisplay("https://www.example.com/"), "example.com");
  EXPECT_EQ(FormatUrlForDisplay("http://example.com/a/b?c=d#e"),
            "example.com/a/b?c=d#e");
  EXPECT_EQ(FormatUrlForDisplay("HTTPS://WWW.Example.com/path/"),
            "Example.com/path/");
}

TEST(UrlDisplayTest, KeepsOtherSchemes) {
  EXPECT_EQ(FormatUrlForDisplay("file:///Users/me/a.pdf"),
            "file:///Users/me/a.pdf");
  EXPECT_EQ(FormatUrlForDisplay("view-source:https://example.com/"),
            "view-source:https://example.com/");
}

TEST(UrlDisplayTest, BlankPagesAreEmpty) {
  EXPECT_EQ(FormatUrlForDisplay(""), "");
  EXPECT_EQ(FormatUrlForDisplay("about:blank"), "");
}

TEST(UrlDisplayTest, KeepsCredentialsVisible) {
  EXPECT_EQ(FormatUrlForDisplay("https://user@www.example.com/"),
            "https://user@www.example.com/");
}

TEST(UrlDisplayTest, KeepsTrailingSlashWithQuery) {
  EXPECT_EQ(FormatUrlForDisplay("https://example.com/?q=1"),
            "example.com/?q=1");
  EXPECT_EQ(FormatUrlForDisplay("https://www."), "www.");
}

}  // namespace
}  // namespace brouk
