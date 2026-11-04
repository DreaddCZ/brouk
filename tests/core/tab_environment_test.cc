#include "core/tab_environment.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace brouk {
namespace {

TEST(TabEnvironmentTest, ChecksValuesFromThePage) {
  TabEnvironment env;
  EXPECT_TRUE(env.empty());
  EXPECT_FALSE(TabEnvironmentProblem(env));
  env.time_zone = "America/Argentina/Buenos_Aires";
  env.languages = {"de-DE", "zh-Hant-TW", "en"};
  env.location = TabEnvironment::Location{.latitude = 35.68,
                                          .longitude = 139.69,
                                          .accuracy = 150,
                                          .label = "Tokyo",
                                          .unavailable = false};
  env.user_agent_preset = "chrome-android";
  env.color_scheme = "dark";
  EXPECT_FALSE(env.empty());
  EXPECT_FALSE(TabEnvironmentProblem(env));

  TabEnvironment bad = env;
  bad.time_zone = "Asia/Tokyo\"}";
  EXPECT_TRUE(TabEnvironmentProblem(bad));
  bad = env;
  bad.languages = {"de_DE"};
  EXPECT_TRUE(TabEnvironmentProblem(bad));
  bad.languages = {"-de"};
  EXPECT_TRUE(TabEnvironmentProblem(bad));
  bad = env;
  bad.location->latitude = 91;
  EXPECT_TRUE(TabEnvironmentProblem(bad));
  bad = env;
  bad.location->accuracy = 0;
  EXPECT_TRUE(TabEnvironmentProblem(bad));
  bad = env;
  bad.user_agent_preset = "netscape";
  EXPECT_TRUE(TabEnvironmentProblem(bad));
  bad = env;
  bad.user_agent = "Custom";
  EXPECT_TRUE(TabEnvironmentProblem(bad));  // Preset and text.
  bad.user_agent_preset.clear();
  EXPECT_FALSE(TabEnvironmentProblem(bad));
  bad.user_agent = "Line\nbreak";
  EXPECT_TRUE(TabEnvironmentProblem(bad));
  bad = env;
  bad.color_scheme = "sepia";
  EXPECT_TRUE(TabEnvironmentProblem(bad));
}

TEST(TabEnvironmentTest, PresetsUseTheChromeVersion) {
  const UserAgentPreset* edge = FindUserAgentPreset("edge-windows");
  ASSERT_NE(edge, nullptr);
  EXPECT_EQ(PresetUserAgent(*edge, 156),
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/156.0.0.0 Safari/537.36 "
            "Edg/156.0.0.0");
  EXPECT_EQ(FindUserAgentPreset("nothing"), nullptr);
  for (const UserAgentPreset& preset : UserAgentPresets()) {
    EXPECT_FALSE(preset.label.empty());
    EXPECT_EQ(PresetUserAgent(preset, 156).find("{v}"), std::string::npos);
  }
}

TEST(TabEnvironmentTest, Summary) {
  TabEnvironment env;
  EXPECT_EQ(TabEnvironmentSummary(env), "");
  env.time_zone = "Asia/Tokyo";
  env.languages = {"de-DE", "de"};
  env.location = TabEnvironment::Location{.latitude = 50.08,
                                          .longitude = 14.44,
                                          .accuracy = 150,
                                          .label = "",
                                          .unavailable = false};
  env.user_agent_preset = "safari-iphone";
  env.reduced_motion = true;
  EXPECT_EQ(TabEnvironmentSummary(env),
            "time zone Asia/Tokyo, language de-DE, de, location 50.0800, "
            "14.4400, user agent Safari on iPhone, reduced motion");
}

}  // namespace
}  // namespace brouk
