#include "core/link_unwrap.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/link_routing_rules.h"

namespace brouk {
namespace {

UnwrappedLink Unwrap(const std::string& url,
                     const std::vector<LinkWrapperRule>& user = {}) {
  return UnwrapLink(url, user);
}

void ExpectDecoded(const std::string& link,
                   const std::string& real,
                   const std::string& wrapper) {
  UnwrappedLink result = Unwrap(link);
  EXPECT_EQ(result.kind, UnwrappedLink::Kind::kDecoded) << link;
  EXPECT_EQ(result.url, real) << link;
  EXPECT_EQ(result.wrapper, wrapper) << link;
}

TEST(LinkUnwrapTest, PlainLinksStayAsTheyAre) {
  UnwrappedLink result = Unwrap("https://companya.atlassian.net/browse/X-1");
  EXPECT_EQ(result.kind, UnwrappedLink::Kind::kPlain);
  EXPECT_EQ(result.url, "https://companya.atlassian.net/browse/X-1");
  EXPECT_EQ(result.wrapper, "");
  // A Google page that is not the redirect.
  EXPECT_EQ(Unwrap("https://www.google.com/search?q=x").kind,
            UnwrappedLink::Kind::kPlain);
}

// What the log may show of a link: never query values, user info or the
// fragment, as they can hold tokens.
TEST(LinkUnwrapTest, LinkForLog) {
  EXPECT_EQ(LinkForLog("https://user:pw@Statics.Teams.cdn.office.net:443/"
                       "evergreen-assets/safelinks/2/atp-safelinks.html"
                       "?url=https%3A%2F%2Fx&locale=en&sd=secret#frag"),
            "statics.teams.cdn.office.net/evergreen-assets/safelinks/2/"
            "atp-safelinks.html ?url&locale&sd");
  EXPECT_EQ(LinkForLog("https://example.com"), "example.com/");
  EXPECT_EQ(LinkForLog("https://example.com/" + std::string(100, 'a')),
            "example.com/" + std::string(59, 'a') + "…");
  EXPECT_EQ(LinkForLog("https://example.com/?=1&a=2&&b"), "example.com/ ?a&b");
  EXPECT_EQ(LinkForLog("mailto:x@example.com"), "(not an http(s) link)");
}

TEST(LinkUnwrapTest, SafeLinks) {
  ExpectDecoded(
      "https://eur02.safelinks.protection.outlook.com/"
      "?url=https%3A%2F%2Fcompanya.atlassian.net%2Fbrowse%2FX-1&data=05%7C01"
      "%7Cuser%40example.com%7C1234%7C0&sdata=abc%3D&reserved=0",
      "https://companya.atlassian.net/browse/X-1", "Safe Links");
  ExpectDecoded(
      "https://statics.teams.cdn.office.net/evergreen-assets/safelinks/1/"
      "atp-safelinks.html?url=https%3A%2F%2Fexample.com%2Fa%3Fb%3D1"
      "&locale=en-us&dest=https%3A%2F%2Fteams.microsoft.com",
      "https://example.com/a?b=1", "Safe Links");
}

TEST(LinkUnwrapTest, ProofpointV1AndV2) {
  ExpectDecoded(
      "https://urldefense.proofpoint.com/v1/url?u=http://www.example.com/"
      "a%3Fb%3D1%26amp;c%3D2&k=abc%3D%0A&r=x&m=y&s=z",
      "http://www.example.com/a?b=1&c=2", "URL Defense");
  ExpectDecoded(
      "https://urldefense.proofpoint.com/v2/url?u=https-3A__www.example.com_"
      "path-3Fa-3D1-26b-3D2&d=DwMFaQ&c=abc&r=def&m=ghi&s=jkl&e=",
      "https://www.example.com/path?a=1&b=2", "URL Defense");
}

TEST(LinkUnwrapTest, ProofpointV3) {
  // Proofpoint's own example: "*" stands for "+" (base64 "Kw").
  ExpectDecoded(
      "https://urldefense.com/v3/__https://google.com:443/search?q=a*test&gs="
      "ps__;Kw!-612Flbf0JvQ3kNJkRi5Jg!Ue6tQudNKaShHg93trcdjqDP8se2ySE65jyCIe2"
      "K1D_uNjZ1Lnf6YLQERujngZv9UWf66ujQIQ$",
      "https://google.com:443/search?q=a+test&gs=ps", "URL Defense");
  // "**A" is a run of two characters ("xy" is "eHk"), then a single one.
  ExpectDecoded(
      "https://urldefense.com/v3/__https://example.com/**A/*__;eHk_"
      "!!ORG!hash$",
      "https://example.com/xy/?", "URL Defense");
  // Non-ASCII replacement characters count as one each ("é!" is "w6kh").
  ExpectDecoded(
      "https://urldefense.com/v3/__https://example.com/caf*/*__;w6kh"
      "!!x$",
      "https://example.com/caf\xc3\xa9/!", "URL Defense");
  // More "*" than characters: cannot be decoded, so it is opaque.
  EXPECT_EQ(
      Unwrap("https://urldefense.com/v3/__https://example.com/**__;Kw!!x$")
          .kind,
      UnwrappedLink::Kind::kOpaque);
}

TEST(LinkUnwrapTest, BarracudaCiscoAndGoogle) {
  ExpectDecoded(
      "https://linkprotect.cudasvc.com/url?a=https%3a%2f%2fexample.com%2fpage"
      "&c=E,1,abc&typo=1",
      "https://example.com/page", "Barracuda");
  ExpectDecoded(
      "https://secure-web.cisco.com/1AbCdEf-ghIJ_kl/"
      "https%3A%2F%2Fexample.com%2Fpage%3Fx%3D1",
      "https://example.com/page?x=1", "Cisco Secure Email");
  ExpectDecoded(
      "https://www.google.com/url?sa=t&url=https%3A%2F%2Fexample.com%2F&usg=x",
      "https://example.com/", "Google");
  ExpectDecoded("https://www.google.com/url?q=https://example.com/a&sa=D",
                "https://example.com/a", "Google");
}

TEST(LinkUnwrapTest, NestedWrappersAreUnwrappedAndNamedByTheOutermost) {
  // Safe Links around URL Defense v2 around the real page.
  ExpectDecoded(
      "https://nam12.safelinks.protection.outlook.com/?url=https%3A%2F%2F"
      "urldefense.proofpoint.com%2Fv2%2Furl%3Fu%3Dhttps-3A__companya.atlassian"
      ".net_browse_X-2D1%26d%3DDwMF&data=x",
      "https://companya.atlassian.net/browse/X-1", "Safe Links");
}

TEST(LinkUnwrapTest, TooManyLevelsAreOpaque) {
  std::string url = "https://example.com/";
  for (int i = 0; i < 6; ++i) {
    std::string encoded;
    for (char c : url) {
      if (c == ':' || c == '/' || c == '?' || c == '=' || c == '&' ||
          c == '%') {
        static const char* kHex = "0123456789ABCDEF";
        encoded += '%';
        encoded += kHex[static_cast<unsigned char>(c) >> 4];
        encoded += kHex[static_cast<unsigned char>(c) & 15];
      } else {
        encoded += c;
      }
    }
    url = "https://www.google.com/url?q=" + encoded;
    if (i == 4) {
      // Five levels still decode.
      EXPECT_EQ(Unwrap(url).kind, UnwrappedLink::Kind::kDecoded);
      EXPECT_EQ(Unwrap(url).url, "https://example.com/");
    }
  }
  UnwrappedLink result = Unwrap(url);
  EXPECT_EQ(result.kind, UnwrappedLink::Kind::kOpaque);
  EXPECT_EQ(result.url, url);
}

TEST(LinkUnwrapTest, UntrustedDecodedUrlsMakeTheLinkOpaque) {
  for (const char* link :
       {"https://eur02.safelinks.protection.outlook.com/?url=javascript%3A"
        "alert(1)",
        "https://eur02.safelinks.protection.outlook.com/?url=file%3A%2F%2F%2F"
        "etc%2Fpasswd",
        "https://eur02.safelinks.protection.outlook.com/?url=https%3A%2F%2F"
        "exa%20mple.com",
        "https://eur02.safelinks.protection.outlook.com/?data=no-url",
        "https://urldefense.proofpoint.com/v9/unknown"}) {
    UnwrappedLink result = Unwrap(link);
    EXPECT_EQ(result.kind, UnwrappedLink::Kind::kOpaque) << link;
    EXPECT_EQ(result.url, link);
    EXPECT_FALSE(result.wrapper.empty());
  }
}

TEST(LinkUnwrapTest, Mimecast) {
  for (const char* link :
       {"https://protect-eu.mimecast.com/s/AbCdEfGh?domain=example.com",
        "https://protect-us.mimecast.com/s/xyz",
        "https://url.uk.m.mimecastprotect.com/s/AbC?domain=example.com"}) {
    UnwrappedLink result = Unwrap(link);
    EXPECT_EQ(result.kind, UnwrappedLink::Kind::kOpaque) << link;
    EXPECT_EQ(result.wrapper, "Mimecast");
  }
  EXPECT_EQ(Unwrap("https://www.mimecast.com/").kind,
            UnwrappedLink::Kind::kPlain);
}

TEST(LinkUnwrapTest, UserWrappers) {
  std::vector<LinkWrapperRule> user = {{"links.corp.example", "target"},
                                       {"*.opaque.example", ""}};
  UnwrappedLink decoded = Unwrap(
      "https://links.corp.example/r?target=https%3A%2F%2Fexample.com%2F", user);
  EXPECT_EQ(decoded.kind, UnwrappedLink::Kind::kDecoded);
  EXPECT_EQ(decoded.url, "https://example.com/");
  EXPECT_EQ(decoded.wrapper, "links.corp.example");

  UnwrappedLink opaque = Unwrap("https://go.opaque.example/abc", user);
  EXPECT_EQ(opaque.kind, UnwrappedLink::Kind::kOpaque);
  EXPECT_EQ(opaque.wrapper, "*.opaque.example");

  // The user's entry wins over the built-in one for the same host.
  std::vector<LinkWrapperRule> override_google = {{"www.google.com", ""}};
  EXPECT_EQ(Unwrap("https://www.google.com/url?q=https://example.com/",
                   override_google)
                .kind,
            UnwrappedLink::Kind::kOpaque);
}

TEST(LinkUnwrapTest, WrapperNames) {
  EXPECT_EQ(LinkWrapperName("https://protect-eu.mimecast.com/s/x", {}),
            "Mimecast");
  EXPECT_EQ(LinkWrapperName("https://example.com/", {}), std::nullopt);
}

TEST(LinkUnwrapTest, BuiltInListForSettings) {
  ASSERT_EQ(BuiltInLinkWrappers().size(), 6u);
  EXPECT_TRUE(BuiltInLinkWrappers().back().opaque);
}

TEST(LinkUnwrapTest, RoutableUrls) {
  EXPECT_TRUE(IsRoutableUrl("https://example.com"));
  EXPECT_TRUE(IsRoutableUrl("HTTP://user@example.com:8080/a?b#c"));
  EXPECT_TRUE(IsRoutableUrl("https://[::1]/"));
  EXPECT_FALSE(IsRoutableUrl("ftp://example.com/"));
  EXPECT_FALSE(IsRoutableUrl("https:///path"));
  EXPECT_FALSE(IsRoutableUrl("https://exa mple.com/"));
  EXPECT_FALSE(IsRoutableUrl("https://example.com/\n"));
  EXPECT_FALSE(IsRoutableUrl("https://ex<ample.com/"));
  EXPECT_FALSE(IsRoutableUrl("example.com"));
}

}  // namespace
}  // namespace brouk
