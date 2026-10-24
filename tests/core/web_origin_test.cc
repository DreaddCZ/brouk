#include "core/web_origin.h"

#include <gtest/gtest.h>

namespace brouk {
namespace {

TEST(WebOriginTest, OriginOfUrl) {
  EXPECT_EQ(WebOriginOf("https://Example.com/login?x=1#a"),
            "https://example.com");
  EXPECT_EQ(WebOriginOf("https://example.com:443/"), "https://example.com");
  EXPECT_EQ(WebOriginOf("http://example.com:80"), "http://example.com");
  EXPECT_EQ(WebOriginOf("http://localhost:8080/x"), "http://localhost:8080");
  EXPECT_EQ(WebOriginOf("https://user:pw@example.com/"), "https://example.com");
  EXPECT_EQ(WebOriginOf("https://[::1]:8443/"), "https://[::1]:8443");
  EXPECT_FALSE(WebOriginOf("brouk://settings"));
  EXPECT_FALSE(WebOriginOf("file:///tmp/a.html"));
  EXPECT_FALSE(WebOriginOf("https:///path"));
  EXPECT_FALSE(WebOriginOf("example.com"));
}

TEST(WebOriginTest, Parts) {
  EXPECT_EQ(SchemeOfOrigin("https://a.example.com:8443"), "https");
  EXPECT_EQ(HostOfOrigin("https://a.example.com:8443"), "a.example.com");
  EXPECT_EQ(HostOfOrigin("https://[::1]:8443"), "[::1]");
  EXPECT_EQ(HostOfOrigin("https://[::1]"), "[::1]");
}

TEST(WebOriginTest, SiteUsesPublicSuffixList) {
  EXPECT_EQ(SiteOfHost("login.company.com"), "company.com");
  EXPECT_EQ(SiteOfHost("www.example.co.uk"), "example.co.uk");
  EXPECT_EQ(SiteOfHost("user.github.io"), "user.github.io");
  EXPECT_EQ(SiteOfHost("WWW.Seznam.CZ"), "seznam.cz");
  EXPECT_EQ(SiteOfHost("localhost"), "localhost");
  EXPECT_EQ(SiteOfHost("192.168.1.10"), "192.168.1.10");
  EXPECT_EQ(SiteOfHost("[::1]"), "[::1]");
}

}  // namespace
}  // namespace brouk
