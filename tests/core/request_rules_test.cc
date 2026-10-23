#include "core/request_rules.h"

#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/yaml_io.h"

namespace brouk {
namespace {

RequestRule Rule(std::string pattern,
                 RequestRule::Action action,
                 std::string header = "",
                 std::string value = "") {
  return {std::move(pattern), action, std::move(header), std::move(value),
          true};
}

TEST(RequestRulesTest, Validation) {
  using A = RequestRule::Action;
  EXPECT_FALSE(ValidateRequestRule(
      Rule("api.example.com", A::kSetRequestHeader, "X-Env", "test")));
  EXPECT_TRUE(ValidateRequestRule(Rule("not a pattern", A::kBlock)));
  EXPECT_TRUE(ValidateRequestRule(
      Rule("a.com", A::kSetRequestHeader, "Bad Header", "x")));
  EXPECT_TRUE(ValidateRequestRule(
      Rule("a.com", A::kSetRequestHeader, "X", "a\r\nInjected: 1")));
  EXPECT_TRUE(ValidateRequestRule(
      Rule("a.com", A::kRedirect, "", "javascript:alert(1)")));
  EXPECT_FALSE(
      ValidateRequestRule(Rule("a.com", A::kRedirect, "", "https://b.com/")));
}

TEST(RequestRulesTest, YamlRoundTripAndProblems) {
  using A = RequestRule::Action;
  RequestRuleSet set;
  set.paused = true;
  set.rules = {
      Rule("*.example.com", A::kSetRequestHeader, "X-Env", "staging"),
      Rule("ads.example.com", A::kBlock),
      Rule("old.example.com/", A::kRedirect, "", "https://new.example.com/")};
  set.rules[1].enabled = false;
  std::vector<std::string> problems;
  Result<YamlNode> parsed = ParseYaml(EmitYaml(RequestRulesToYaml(set)));
  ASSERT_TRUE(parsed);
  RequestRuleSet back = RequestRulesFromYaml(parsed.value(), problems);
  EXPECT_TRUE(problems.empty());
  EXPECT_TRUE(back.paused);
  ASSERT_EQ(back.rules.size(), 3u);
  EXPECT_EQ(back.rules[0].value, "staging");
  EXPECT_FALSE(back.rules[1].enabled);
  EXPECT_EQ(back.rules[2].value, "https://new.example.com/");

  Result<YamlNode> broken = ParseYaml(
      "rules:\n"
      "  - pattern: a.com\n    action: explode\n"
      "  - pattern: a.com\n    action: set-request-header\n    header: \"X "
      "Y\"\n"
      "  - pattern: b.com\n    action: block\n");
  ASSERT_TRUE(broken);
  problems.clear();
  RequestRuleSet partial = RequestRulesFromYaml(broken.value(), problems);
  EXPECT_EQ(problems.size(), 2u);
  ASSERT_EQ(partial.rules.size(), 1u);
  EXPECT_EQ(partial.rules[0].pattern, "b.com");
}

TEST(RequestRulesTest, HeadersMostSpecificWins) {
  using A = RequestRule::Action;
  RequestRuleSet set;
  set.rules = {Rule("api.example.com", A::kSetRequestHeader, "X-Env", "exact"),
               Rule("*.example.com", A::kSetRequestHeader, "X-Env", "wildcard"),
               Rule("*.example.com", A::kRemoveRequestHeader, "Cookie"),
               Rule("other.com", A::kSetRequestHeader, "X-Other", "1")};
  Headers headers = ApplyRequestHeaderRules(
      set, "https://api.example.com/v1",
      {{"cookie", "s=1"}, {"x-env", "page"}, {"Accept", "*/*"}});
  EXPECT_EQ(headers, (Headers{{"Accept", "*/*"}, {"X-Env", "exact"}}));
  set.paused = true;
  EXPECT_EQ(
      ApplyRequestHeaderRules(set, "https://api.example.com/", {{"A", "b"}}),
      (Headers{{"A", "b"}}));
}

TEST(RequestRulesTest, BlockAndRedirect) {
  using A = RequestRule::Action;
  RequestRuleSet set;
  set.rules = {Rule("*.example.com", A::kBlock),
               Rule("cdn.example.com/lib/", A::kRedirect, "",
                    "http://localhost:3000/lib.js"),
               Rule("*.example.com", A::kSetResponseHeader,
                    "Access-Control-Allow-Origin", "*")};
  EXPECT_TRUE(DecideRequest(set, "https://ads.example.com/x").block);
  RequestRuleDecision lib =
      DecideRequest(set, "https://cdn.example.com/lib/app.js");
  EXPECT_FALSE(lib.block);
  EXPECT_EQ(lib.redirect, "http://localhost:3000/lib.js");
  EXPECT_TRUE(lib.changes_response_headers);
  EXPECT_FALSE(DecideRequest(set, "https://other.com/").affected());
  // A redirect to the same URL does nothing (no loop).
  RequestRuleSet loop;
  loop.rules = {Rule("a.com", A::kRedirect, "", "https://a.com/")};
  EXPECT_FALSE(DecideRequest(loop, "https://a.com/").redirect);
  EXPECT_TRUE(HasActiveRules(set));
  set.paused = true;
  EXPECT_FALSE(HasActiveRules(set));
}

TEST(RequestRulesTest, OwnRequestsSkipStreams) {
  EXPECT_TRUE(CanLoadForResponseRules({{"Accept", "application/json"}}));
  EXPECT_FALSE(CanLoadForResponseRules({{"accept", "text/event-stream"}}));
  EXPECT_FALSE(CanLoadForResponseRules({{"Upgrade", "websocket"}}));
}

TEST(RequestRulesTest, OwnRequestHeaders) {
  Headers sent = HeadersForOwnRequest({{"Host", "a.com"},
                                       {"X-Env", "staging"},
                                       {"content-length", "3"},
                                       {"Referer", "https://a.com/"},
                                       {"Accept-Encoding", "gzip"},
                                       {"Accept", "*/*"}});
  EXPECT_EQ(sent, (Headers{{"X-Env", "staging"}, {"Accept", "*/*"}}));
  Headers answered =
      HeadersForLoadedResponse({{"Content-Type", "application/json"},
                                {"Content-Encoding", "gzip"},
                                {"Content-Length", "120"},
                                {"transfer-encoding", "chunked"},
                                {"Set-Cookie", "a=1"}});
  EXPECT_EQ(answered, (Headers{{"Content-Type", "application/json"},
                               {"Set-Cookie", "a=1"}}));
}

TEST(RequestRulesTest, FileRoundTrip) {
  std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "brouk-rules-test";
  std::filesystem::remove_all(dir);
  RequestRuleSet set;
  set.rules = {Rule("a.com", RequestRule::Action::kBlock)};
  ASSERT_FALSE(SaveRequestRules(dir / "request-rules.yaml", set));
  std::vector<std::string> problems;
  RequestRuleSet back = LoadRequestRules(dir / "request-rules.yaml", problems);
  EXPECT_TRUE(problems.empty());
  EXPECT_EQ(back.rules.size(), 1u);
  EXPECT_TRUE(LoadRequestRules(dir / "missing.yaml", problems).rules.empty());
  std::filesystem::remove_all(dir);
}

}  // namespace
}  // namespace brouk
