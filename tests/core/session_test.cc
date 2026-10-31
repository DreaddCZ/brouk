#include "core/session.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <gtest/gtest.h>

#include "core/yaml_io.h"

namespace brouk {
namespace {

Session TwoWindows() {
  Session session;
  session.clean_exit = false;
  session.windows.push_back({.tabs = {{.url = "https://a.com/",
                                       .title = "A \"quoted\"",
                                       .profile = "default",
                                       .pinned = true},
                                      {.url = "https://b.com/?q=1#x",
                                       .title = "",
                                       .profile = "p-work",
                                       .pinned = false}},
                             .active = 1,
                             .collapsed = {"p-work"},
                             .x = 10,
                             .y = 20,
                             .width = 1200,
                             .height = 800,
                             .maximized = true});
  session.windows.push_back({.tabs = {{.url = "https://c.com/",
                                       .title = "C",
                                       .profile = "default",
                                       .pinned = false}},
                             .active = 0,
                             .collapsed = {},
                             .x = 0,
                             .y = 0,
                             .width = 0,
                             .height = 0,
                             .maximized = false});
  return session;
}

TEST(SessionTest, RoundTripsThroughYaml) {
  Session session = TwoWindows();
  Result<YamlNode> parsed = ParseYaml(EmitYaml(SessionToYaml(session)));
  ASSERT_TRUE(parsed);
  Session back = SessionFromYaml(parsed.value());
  EXPECT_FALSE(back.clean_exit);
  ASSERT_EQ(back.windows.size(), 2U);
  const SessionWindow& first = back.windows[0];
  ASSERT_EQ(first.tabs.size(), 2U);
  EXPECT_EQ(first.tabs[0].title, "A \"quoted\"");
  EXPECT_TRUE(first.tabs[0].pinned);
  EXPECT_EQ(first.tabs[1].url, "https://b.com/?q=1#x");
  EXPECT_EQ(first.tabs[1].profile, "p-work");
  EXPECT_EQ(first.active, 1U);
  EXPECT_EQ(first.collapsed, (std::vector<ProfileId>{"p-work"}));
  EXPECT_EQ(first.y, 20);
  EXPECT_EQ(first.width, 1200);
  EXPECT_TRUE(first.maximized);
  EXPECT_FALSE(back.windows[1].maximized);
  EXPECT_EQ(back.windows[1].width, 0);  // No frame saved.
}

TEST(SessionTest, InvalidEntriesAreSkipped) {
  Result<YamlNode> parsed = ParseYaml(
      "clean_exit: true\n"
      "windows:\n"
      "  - tabs:\n"
      "      - url: \"\"\n"
      "        profile: default\n"
      "      - url: https://ok.com/\n"
      "        profile: \"../etc\"\n"
      "      - url: https://ok.com/\n"
      "        profile: default\n"
      "    active: 2\n"
      "    width: 50\n"
      "    height: 50\n"
      "    x: 0\n"
      "    y: 0\n"
      "  - tabs: []\n"
      "  - not a window\n");
  ASSERT_TRUE(parsed);
  Session session = SessionFromYaml(parsed.value());
  EXPECT_TRUE(session.clean_exit);
  ASSERT_EQ(session.windows.size(), 1U);
  ASSERT_EQ(session.windows[0].tabs.size(), 1U);
  EXPECT_EQ(session.windows[0].active, 0U);  // Index of the kept tab.
  EXPECT_EQ(session.windows[0].width, 0);    // Too small: ignored.
}

TEST(SessionTest, SavesAndLoadsFiles) {
  std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "brouk-session-test";
  std::error_code error;
  std::filesystem::remove_all(dir, error);
  std::filesystem::create_directories(dir);
  std::filesystem::path path = dir / "session.yaml";
  Result<Session> missing = LoadSession(path);
  ASSERT_TRUE(missing);
  EXPECT_TRUE(missing.value().windows.empty());
  EXPECT_TRUE(missing.value().clean_exit);
  ASSERT_FALSE(SaveSession(path, TwoWindows()));
  Result<Session> loaded = LoadSession(path);
  ASSERT_TRUE(loaded);
  EXPECT_EQ(loaded.value().windows.size(), 2U);
  EXPECT_FALSE(std::filesystem::exists(dir / "session.yaml.tmp"));
  std::ofstream(path) << "windows: [unclosed";
  EXPECT_FALSE(LoadSession(path));
  std::filesystem::remove_all(dir, error);
}

TEST(SessionTest, StartupDecision) {
  Session empty;
  Session crashed = TwoWindows();
  Session clean = TwoWindows();
  clean.clean_exit = true;
  EXPECT_EQ(DecideStartup("last_session", empty), StartupAction::kNewWindow);
  EXPECT_EQ(DecideStartup("last_session", clean), StartupAction::kRestore);
  EXPECT_EQ(DecideStartup("last_session", crashed), StartupAction::kRestore);
  EXPECT_EQ(DecideStartup("new_tab", clean), StartupAction::kNewWindow);
  EXPECT_EQ(DecideStartup("new_tab", crashed),
            StartupAction::kNewWindowOfferRestore);
}

}  // namespace
}  // namespace brouk
