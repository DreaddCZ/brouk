#include "core/url_pattern.h"

#include <gtest/gtest.h>

namespace brouk {
namespace {

TEST(UrlPatternTest, Parsing) {
  EXPECT_EQ(UrlPattern::Parse("Example.COM")->host, "example.com");
  EXPECT_TRUE(UrlPattern::Parse("*.example.com")->subdomains);
  EXPECT_EQ(UrlPattern::Parse("example.com:8443")->port, 8443);
  EXPECT_EQ(UrlPattern::Parse("https://example.com/api/")->path_prefix,
            "/api/");
  EXPECT_EQ(UrlPattern::Parse(" *.a.b:81/x ")->ToString(), "*.a.b:81/x");
  EXPECT_EQ(UrlPattern::Parse("github.com/company-a/*")->path_prefix,
            "/company-a/");
  EXPECT_FALSE(UrlPattern::Parse(""));
  EXPECT_FALSE(UrlPattern::Parse("*."));
  EXPECT_FALSE(UrlPattern::Parse("exa mple.com"));
  EXPECT_FALSE(UrlPattern::Parse("example.com:0"));
  EXPECT_FALSE(UrlPattern::Parse("example.com:http"));
  EXPECT_FALSE(UrlPattern::Parse("example.com/a?b"));
  EXPECT_FALSE(UrlPattern::Parse(".example.com"));
}

TEST(UrlPatternTest, Matching) {
  UrlPattern host = *UrlPattern::Parse("api.example.com");
  EXPECT_TRUE(host.Matches("https://api.example.com/v1"));
  EXPECT_TRUE(host.Matches("http://API.example.com:8080/"));
  EXPECT_FALSE(host.Matches("https://x.api.example.com/"));
  EXPECT_FALSE(host.Matches("https://api.example.com.evil.com/"));
  EXPECT_FALSE(host.Matches("ftp://api.example.com/"));

  UrlPattern wildcard = *UrlPattern::Parse("*.example.com");
  EXPECT_TRUE(wildcard.Matches("https://example.com/"));
  EXPECT_TRUE(wildcard.Matches("https://a.b.example.com/"));
  EXPECT_FALSE(wildcard.Matches("https://badexample.com/"));

  UrlPattern path = *UrlPattern::Parse("example.com:8443/api/");
  EXPECT_TRUE(path.Matches("https://example.com:8443/api/users?x=1"));
  EXPECT_FALSE(path.Matches("https://example.com/api/users"));
  EXPECT_FALSE(path.Matches("https://example.com:8443/apix"));
  EXPECT_FALSE(UrlPattern::Parse("a.com")->Matches("https://user@b.com/"));
  EXPECT_TRUE(UrlPattern::Parse("b.com")->Matches("https://a.com@b.com/"));
}

// Link routing (010): Azure names the tenant in the fragment.
TEST(UrlPatternTest, FragmentOnlyWhereAllowed) {
  constexpr std::string_view kAzure =
      "https://portal.azure.com/#@contoso.onmicrosoft.com/xxxx";
  EXPECT_FALSE(UrlPattern::Parse(kAzure));
  std::optional<UrlPattern> pattern =
      UrlPattern::Parse(kAzure, UrlPattern::Fragment::kAllowed);
  ASSERT_TRUE(pattern);
  EXPECT_EQ(pattern->path_prefix, "/");
  EXPECT_EQ(pattern->fragment_prefix, "@contoso.onmicrosoft.com/xxxx");
  EXPECT_EQ(pattern->ToString(),
            "portal.azure.com/#@contoso.onmicrosoft.com/xxxx");
  // "/*" ends a fragment prefix as it ends a path prefix; an empty fragment
  // is no fragment.
  EXPECT_EQ(UrlPattern::Parse("portal.azure.com/#@a.com/*",
                              UrlPattern::Fragment::kAllowed)
                ->fragment_prefix,
            "@a.com/");
  EXPECT_EQ(UrlPattern::Parse("example.com/#", UrlPattern::Fragment::kAllowed)
                ->ToString(),
            "example.com/");
  EXPECT_FALSE(
      UrlPattern::Parse("example.com/#a b", UrlPattern::Fragment::kAllowed));
  EXPECT_FALSE(
      UrlPattern::Parse("example.com/a?b#c", UrlPattern::Fragment::kAllowed));
}

TEST(UrlPatternTest, MatchesFragmentPrefix) {
  UrlPattern tenant = *UrlPattern::Parse(
      "portal.azure.com/#@a.onmicrosoft.com/", UrlPattern::Fragment::kAllowed);
  EXPECT_TRUE(tenant.Matches(
      "https://portal.azure.com/#@a.onmicrosoft.com/resource/x"));
  EXPECT_TRUE(tenant.Matches(
      "https://portal.azure.com/?feature=1#@a.onmicrosoft.com/resource/x"));
  EXPECT_FALSE(tenant.Matches(
      "https://portal.azure.com/#@b.onmicrosoft.com/resource/x"));
  EXPECT_FALSE(tenant.Matches("https://portal.azure.com/#home"));
  EXPECT_FALSE(tenant.Matches("https://portal.azure.com/"));
  EXPECT_TRUE(UrlPattern::Parse("a.com/#x", UrlPattern::Fragment::kAllowed)
                  ->Matches("https://a.com#x"));

  UrlPattern path = *UrlPattern::Parse("example.com/app/#/settings",
                                       UrlPattern::Fragment::kAllowed);
  EXPECT_TRUE(path.Matches("https://example.com/app/x#/settings/a"));
  EXPECT_FALSE(path.Matches("https://example.com/other#/settings"));
}

TEST(UrlPatternTest, MoreSpecificWins) {
  EXPECT_GT(UrlPattern::Parse("api.example.com")->Specificity(),
            UrlPattern::Parse("*.example.com/very/long/path")->Specificity());
  EXPECT_GT(UrlPattern::Parse("example.com/api/v2/")->Specificity(),
            UrlPattern::Parse("example.com/api/")->Specificity());
  EXPECT_GT(UrlPattern::Parse("example.com:8080")->Specificity(),
            UrlPattern::Parse("example.com/a")->Specificity());
  EXPECT_GT(UrlPattern::Parse("example.com/#a", UrlPattern::Fragment::kAllowed)
                ->Specificity(),
            UrlPattern::Parse("example.com/")->Specificity());
}

TEST(UrlPatternListTest, ParsesCommaSeparatedPatterns) {
  std::optional<std::vector<UrlPattern>> list =
      ParseUrlPatternList(" login.microsoftonline.com, *.okta.com ,,");
  ASSERT_TRUE(list);
  ASSERT_EQ(list->size(), 2U);
  EXPECT_TRUE(MatchesAnyPattern(*list, "https://acme.okta.com/app"));
  EXPECT_TRUE(
      MatchesAnyPattern(*list, "https://login.microsoftonline.com/x?y=1"));
  EXPECT_FALSE(MatchesAnyPattern(*list, "https://example.com/"));
  EXPECT_TRUE(ParseUrlPatternList("")->empty());
  EXPECT_FALSE(ParseUrlPatternList("example.com, not a pattern"));
}

}  // namespace
}  // namespace brouk
