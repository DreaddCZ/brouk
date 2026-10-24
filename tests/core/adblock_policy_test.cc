#include "core/adblock_policy.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace brouk {
namespace {

TEST(AdblockPolicyTest, ProtectedHosts) {
  for (const char* host :
       {"localhost", "app.localhost", "printer.local", "127.0.0.1", "10.1.2.3",
        "172.16.0.1", "172.31.255.255", "192.168.1.10", "169.254.1.1", "[::1]",
        "[fd00::1]", "LOCALHOST."}) {
    EXPECT_TRUE(IsProtectedHost(host)) << host;
  }
  for (const char* host :
       {"example.com", "172.32.0.1", "11.0.0.1", "192.169.0.1",
        "localhost.example.com", "[2001::1]"}) {
    EXPECT_FALSE(IsProtectedHost(host)) << host;
  }
  std::vector<std::string> routed = {"intranet.corp", "Jira.Example.com"};
  EXPECT_TRUE(IsProtectedHost("intranet.corp", routed));
  EXPECT_TRUE(IsProtectedHost("wiki.intranet.corp", routed));
  EXPECT_TRUE(IsProtectedHost("jira.example.com", routed));
  EXPECT_FALSE(IsProtectedHost("example.com", routed));
}

TEST(AdblockPolicyTest, HostsAndThirdParties) {
  EXPECT_EQ(HostOfUrl("https://User@WWW.Example.com:8443/a?b#c"),
            "www.example.com");
  EXPECT_EQ(HostOfUrl("http://[::1]:8080/"), "[::1]");
  EXPECT_EQ(HostOfUrl("about:blank"), "");
  EXPECT_FALSE(IsThirdParty("cdn.news.example", "www.news.example"));
  EXPECT_TRUE(IsThirdParty("ads.tracker.example", "www.news.example"));
  EXPECT_FALSE(IsThirdParty("static.bbc.co.uk", "www.bbc.co.uk"));
  EXPECT_TRUE(IsThirdParty("x.other.co.uk", "www.bbc.co.uk"));
  EXPECT_FALSE(IsThirdParty("", "a.example"));
}

AdblockMatch Blocked(std::string rule) {
  AdblockMatch match;
  match.block = true;
  match.filter = AdblockRule{.list = 0, .line = 1, .text = std::move(rule)};
  return match;
}

TEST(AdblockPolicyTest, ProtectedHostsNeedARuleNamingThem) {
  std::vector<std::string> none;
  AdblockDecision broad = DecideRequest(Blocked("/ads/*"), "localhost", none);
  EXPECT_FALSE(broad.block);
  EXPECT_TRUE(broad.excepted);
  AdblockDecision named =
      DecideRequest(Blocked("||192.168.1.10^"), "192.168.1.10", none);
  EXPECT_TRUE(named.block);
  EXPECT_TRUE(DecideRequest(Blocked("/ads/*"), "ads.example", none).block);
}

TEST(AdblockPolicyTest, ExceptionsRedirectsAndRewrites) {
  AdblockMatch excepted;
  excepted.filter = AdblockRule{.list = 0, .line = 1, .text = "||a^"};
  excepted.exception = AdblockRule{.list = 1, .line = 2, .text = "@@||a^"};
  EXPECT_TRUE(DecideRequest(excepted, "a.example", {}).excepted);
  AdblockMatch redirect = Blocked("||a^$redirect=noopjs");
  redirect.redirect = "data:application/javascript;base64,KCk=";
  EXPECT_EQ(DecideRequest(redirect, "a.example", {}).redirect,
            redirect.redirect);
  AdblockMatch rewrite;
  rewrite.rewritten_url = "https://a.example/?id=1";
  AdblockDecision decision = DecideRequest(rewrite, "a.example", {});
  EXPECT_FALSE(decision.block);
  EXPECT_EQ(decision.rewritten_url, "https://a.example/?id=1");
}

TEST(AdblockPolicyTest, DataUrls) {
  std::optional<DataUrl> js = ParseDataUrl(
      "data:application/javascript;base64,KGZ1bmN0aW9uKCl7fSkoKTs=");
  ASSERT_TRUE(js);
  EXPECT_EQ(js->mime_type, "application/javascript");
  EXPECT_EQ(js->body, "(function(){})();");
  std::optional<DataUrl> text = ParseDataUrl("data:,a%20b%2");
  ASSERT_TRUE(text);
  EXPECT_EQ(text->mime_type, "text/plain");
  EXPECT_EQ(text->body, "a b%2");
  EXPECT_FALSE(ParseDataUrl("https://a.example/"));
  EXPECT_FALSE(ParseDataUrl("data:image/gif;base64,!!!"));
}

}  // namespace
}  // namespace brouk
