#include "core/link_routing_rules.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "core/yaml_io.h"

namespace brouk {
namespace {

LinkRoutingConfig Parse(const std::string& yaml,
                        std::vector<std::string>& problems) {
  Result<YamlNode> root = ParseYaml(yaml);
  EXPECT_TRUE(root.ok());
  return LinkRoutingFromYaml(root.value(), problems);
}

TEST(LinkRoutingRulesTest, ReadsRulesAndWrappers) {
  std::vector<std::string> problems;
  LinkRoutingConfig config = Parse(R"(
rules:
  - pattern: companya.atlassian.net
    profiles: [p-a]
  - pattern: portal.azure.com
    profiles:
      - p-a
      - p-b
      - default
wrappers:
  - pattern: links.corp.example
    parameter: target
  - pattern: "*.opaque.example"
    opaque: true
)",
                                   problems);
  EXPECT_TRUE(problems.empty());
  ASSERT_EQ(config.rules.size(), 2u);
  EXPECT_EQ(config.rules[0], (RoutingRule{"companya.atlassian.net", {"p-a"}}));
  EXPECT_EQ(config.rules[1].profiles,
            (std::vector<ProfileId>{"p-a", "p-b", "default"}));
  ASSERT_EQ(config.wrappers.size(), 2u);
  EXPECT_EQ(config.wrappers[0],
            (LinkWrapperRule{"links.corp.example", "target"}));
  EXPECT_EQ(config.wrappers[1], (LinkWrapperRule{"*.opaque.example", ""}));
}

TEST(LinkRoutingRulesTest, ReportsAndSkipsInvalidEntries) {
  std::vector<std::string> problems;
  LinkRoutingConfig config = Parse(R"(
rules:
  - pattern: "bad pattern"
    profiles: [p-a]
  - pattern: example.com
    profiles: []
  - pattern: example.com
    profiles: [Not An Id]
  - pattern: example.com
    profiles: [p-a, p-a]
  - pattern: example.com
  - just text
  - pattern: good.example
    profiles: [p-a]
wrappers:
  - pattern: w.example
  - pattern: w.example
    parameter: x
    opaque: true
  - pattern: w.example
    parameter: "a b"
)",
                                   problems);
  ASSERT_EQ(config.rules.size(), 1u);
  EXPECT_EQ(config.rules[0].pattern, "good.example");
  EXPECT_TRUE(config.wrappers.empty());
  EXPECT_EQ(problems.size(), 9u);
  EXPECT_NE(problems[0].find("line 3"), std::string::npos) << problems[0];
}

TEST(LinkRoutingRulesTest, WrongShapes) {
  std::vector<std::string> problems;
  Parse("- a\n- b\n", problems);
  EXPECT_EQ(problems.size(), 1u);
  problems.clear();
  Parse("rules: text\nwrappers: {a: b}\n", problems);
  EXPECT_EQ(problems.size(), 2u);
  problems.clear();
  EXPECT_EQ(Parse("", problems), LinkRoutingConfig());
  EXPECT_TRUE(problems.empty());
}

TEST(LinkRoutingRulesTest, SavesAndLoads) {
  std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "brouk-link-routing-test";
  std::filesystem::remove_all(dir);
  std::filesystem::path path = dir / "link-routing.yaml";
  std::vector<std::string> problems;
  EXPECT_EQ(LoadLinkRouting(path, problems), LinkRoutingConfig());

  LinkRoutingConfig config;
  config.rules = {{"github.com/company-a/*", {"p-a"}},
                  {"*.atlassian.net", {"p-a", "default"}}};
  config.wrappers = {{"links.corp.example", "target"}, {"go.example", ""}};
  EXPECT_FALSE(SaveLinkRouting(path, config));
  EXPECT_EQ(LoadLinkRouting(path, problems), config);
  EXPECT_TRUE(problems.empty());
  std::filesystem::remove_all(dir);
}

TEST(LinkRoutingRulesTest, MostSpecificRuleWins) {
  LinkRoutingConfig config;
  config.rules = {{"*.atlassian.net", {"p-wild"}},
                  {"companya.atlassian.net", {"p-host"}},
                  {"*.atlassian.net/wiki/", {"p-path"}},
                  {"github.com", {"p-github"}},
                  {"github.com/company-a/*", {"p-company"}},
                  {"*.sub.atlassian.net", {"p-sub"}}};
  auto profile = [&](const char* url) {
    const RoutingRule* rule = FindRoutingRule(config, url);
    return rule ? rule->profiles.front() : ProfileId("none");
  };
  EXPECT_EQ(profile("https://other.atlassian.net/browse/X"), "p-wild");
  EXPECT_EQ(profile("https://companya.atlassian.net/browse/X"), "p-host");
  // Path prefix over host over subdomain wildcard.
  EXPECT_EQ(profile("https://companya.atlassian.net/wiki/x"), "p-path");
  EXPECT_EQ(profile("https://github.com/company-a/repo"), "p-company");
  EXPECT_EQ(profile("https://github.com/company-b/repo"), "p-github");
  // The longer wildcard host is more specific.
  EXPECT_EQ(profile("https://x.sub.atlassian.net/"), "p-sub");
  EXPECT_EQ(profile("https://example.com/"), "none");
}

TEST(LinkRoutingRulesTest, EquallySpecificRulesKeepTheirOrder) {
  LinkRoutingConfig config;
  config.rules = {{"example.com", {"p-first"}}, {"example.com", {"p-second"}}};
  EXPECT_EQ(FindRoutingRule(config, "https://example.com/")->profiles.front(),
            "p-first");
}

TEST(LinkRoutingRulesTest, ProfileLinks) {
  LinkRoutingConfig config;
  config.rules = {{"github.com/company-a/", {"p-a"}},
                  {"portal.azure.com", {"p-a", "p-b"}},
                  {"*.sharepoint.com", {"p-b"}}};
  EXPECT_EQ(
      PatternsOfProfile(config, "p-a"),
      (std::vector<std::string>{"github.com/company-a/", "portal.azure.com"}));

  // p-a keeps GitHub (written differently), leaves Azure, joins SharePoint
  // and gets a new rule.
  std::vector<std::string> patterns = {"https://GitHub.com/company-a/*",
                                       "*.sharepoint.com", "jira.example.com",
                                       "jira.example.com"};
  SetProfilePatterns(config, "p-a", patterns);
  ASSERT_EQ(config.rules.size(), 4u);
  EXPECT_EQ(config.rules[0], (RoutingRule{"github.com/company-a/", {"p-a"}}));
  EXPECT_EQ(config.rules[1], (RoutingRule{"portal.azure.com", {"p-b"}}));
  EXPECT_EQ(config.rules[2], (RoutingRule{"*.sharepoint.com", {"p-b", "p-a"}}));
  EXPECT_EQ(config.rules[3], (RoutingRule{"jira.example.com", {"p-a"}}));

  // Removing all of a profile's links deletes rules left without profiles.
  SetProfilePatterns(config, "p-a", {});
  ASSERT_EQ(config.rules.size(), 2u);
  EXPECT_EQ(config.rules[1], (RoutingRule{"*.sharepoint.com", {"p-b"}}));
}

TEST(LinkRoutingRulesTest, NarrowingARule) {
  LinkRoutingConfig config;
  config.rules = {{"portal.azure.com", {"p-a", "p-b", "p-c"}}};
  EXPECT_TRUE(NarrowRoutingRule(config, "portal.azure.com", "p-b"));
  EXPECT_EQ(config.rules[0].profiles, std::vector<ProfileId>{"p-b"});
  EXPECT_FALSE(NarrowRoutingRule(config, "other.com", "p-b"));
}

}  // namespace
}  // namespace brouk
