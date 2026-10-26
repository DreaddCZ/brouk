#include "core/link_router.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace brouk {
namespace {

const std::vector<ProfileId> kExisting = {"default", "p-a", "p-b", "p-c"};

LinkRoutingConfig Config() {
  LinkRoutingConfig config;
  config.rules = {{"companya.atlassian.net", {"p-a"}},
                  {"portal.azure.com", {"p-a", "p-b", "p-c"}},
                  {"gone.example", {"p-deleted"}},
                  {"half.example", {"p-deleted", "p-b"}}};
  return config;
}

ProfileDecision Decide(const char* url,
                       LinkSource source,
                       const ProfileId& from = "default") {
  return DecideProfile(Config(), url, source, from, "default", kExisting);
}

TEST(LinkRouterTest, SingleProfileRuleOpensThere) {
  for (LinkSource source :
       {LinkSource::kOtherApp, LinkSource::kNewTab, LinkSource::kTypedNewTab}) {
    ProfileDecision decision =
        Decide("https://companya.atlassian.net/browse/X-1", source);
    EXPECT_EQ(decision.profiles, std::vector<ProfileId>{"p-a"});
    EXPECT_EQ(decision.rule_pattern, "companya.atlassian.net");
    EXPECT_FALSE(decision.stays_in_source);
  }
}

TEST(LinkRouterTest, SeveralProfilesAskInTheRulesOrder) {
  ProfileDecision decision =
      Decide("https://portal.azure.com/#home", LinkSource::kOtherApp);
  EXPECT_EQ(decision.profiles, (std::vector<ProfileId>{"p-a", "p-b", "p-c"}));
  EXPECT_EQ(decision.rule_pattern, "portal.azure.com");
  // From a profile the rule does not list: ask as well.
  EXPECT_EQ(
      Decide("https://portal.azure.com/", LinkSource::kNewTab).profiles.size(),
      3u);
}

TEST(LinkRouterTest, SourceProfileInTheRuleStays) {
  ProfileDecision decision =
      Decide("https://portal.azure.com/", LinkSource::kNewTab, "p-b");
  EXPECT_EQ(decision.profiles, std::vector<ProfileId>{"p-b"});
  EXPECT_TRUE(decision.stays_in_source);
  // Not for links from other apps: they have no source profile.
  EXPECT_EQ(Decide("https://portal.azure.com/", LinkSource::kOtherApp, "p-b")
                .profiles.size(),
            3u);
}

TEST(LinkRouterTest, NoMatch) {
  ProfileDecision inside =
      Decide("https://example.com/", LinkSource::kNewTab, "p-c");
  EXPECT_EQ(inside.profiles, std::vector<ProfileId>{"p-c"});
  EXPECT_TRUE(inside.stays_in_source);
  EXPECT_EQ(inside.rule_pattern, std::nullopt);

  ProfileDecision outside =
      Decide("https://example.com/", LinkSource::kOtherApp, "p-c");
  EXPECT_EQ(outside.profiles, std::vector<ProfileId>{"default"});
  EXPECT_FALSE(outside.stays_in_source);
}

TEST(LinkRouterTest, DeletedProfilesAreSkipped) {
  EXPECT_EQ(Decide("https://gone.example/", LinkSource::kOtherApp).profiles,
            std::vector<ProfileId>{"default"});
  EXPECT_EQ(Decide("https://half.example/", LinkSource::kOtherApp).profiles,
            std::vector<ProfileId>{"p-b"});
}

RoutingWindow Window(std::int64_t id,
                     std::vector<ProfileId> profiles,
                     bool on_screen = true,
                     bool on_space = true) {
  return {id, std::move(profiles), on_screen, on_space};
}

TEST(LinkRouterTest, NoWindowsOpenANewOne) {
  EXPECT_EQ(DecideWindow("p-a", "default", {}).kind,
            WindowDecision::Kind::kNewWindow);
  EXPECT_EQ(DecideWindow("default", "default", {}).kind,
            WindowDecision::Kind::kNewWindow);
}

TEST(LinkRouterTest, DefaultProfileUsesTheMostRecentWindowHere) {
  std::vector<RoutingWindow> windows = {
      Window(1, {"default"}, false, true),  // Other screen.
      Window(2, {"default"}, true, false),  // This screen, other Space.
      Window(3, {"default"}, true, true),   // Here.
      Window(4, {"default"}, true, true)};
  WindowDecision decision = DecideWindow("default", "default", windows);
  EXPECT_EQ(decision.kind, WindowDecision::Kind::kWindow);
  EXPECT_EQ(decision.window, 3);

  windows = {Window(1, {"default"}, false, true),
             Window(2, {"default"}, true, false)};
  EXPECT_EQ(DecideWindow("default", "default", windows).window, 2);

  // None on this screen: the most recent anywhere.
  windows = {Window(5, {"default"}, false, false),
             Window(6, {"default"}, false, true)};
  EXPECT_EQ(DecideWindow("default", "default", windows).window, 5);
}

TEST(LinkRouterTest, OtherProfilesFollowTheirGroups) {
  std::vector<RoutingWindow> windows = {
      Window(1, {"default"}), Window(2, {"p-a", "default"}),
      Window(3, {"p-b", "default"}), Window(4, {"p-b", "default"})};
  WindowDecision one = DecideWindow("p-a", "default", windows);
  EXPECT_EQ(one.kind, WindowDecision::Kind::kWindow);
  EXPECT_EQ(one.window, 2);

  WindowDecision several = DecideWindow("p-b", "default", windows);
  EXPECT_EQ(several.kind, WindowDecision::Kind::kChoose);
  EXPECT_EQ(several.choices, (std::vector<std::int64_t>{3, 4}));

  WindowDecision none = DecideWindow("p-c", "default", windows);
  EXPECT_EQ(none.kind, WindowDecision::Kind::kNewGroup);
  EXPECT_EQ(none.window, 1);
}

}  // namespace
}  // namespace brouk
