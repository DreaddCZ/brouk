#include "core/settings.h"

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/yaml_io.h"

namespace brouk {
namespace {

Settings Load(SettingScope scope,
              const std::string& yaml,
              std::vector<SettingProblem>& problems) {
  Result<YamlNode> root = ParseYaml(yaml);
  EXPECT_TRUE(root);
  return Settings::FromYaml(scope, root ? root.value() : YamlNode(), problems);
}

TEST(SettingsTest, Defaults) {
  Settings global(SettingScope::kGlobal);
  EXPECT_EQ(global.GetString("profiles.default"), "default");
  EXPECT_FALSE(global.GetBool("profiles.show_manager_at_startup"));
  Settings profile(SettingScope::kProfile);
  EXPECT_EQ(profile.GetString("profile.color"), "blue");
}

TEST(SettingsTest, ReadsValidValues) {
  std::vector<SettingProblem> problems;
  Settings settings =
      Load(SettingScope::kProfile, "profile:\n  name: Work\n  color: green\n",
           problems);
  EXPECT_TRUE(problems.empty());
  EXPECT_EQ(settings.GetString("profile.name"), "Work");
  EXPECT_EQ(settings.GetString("profile.color"), "green");
}

TEST(SettingsTest, InvalidValuesKeepDefaultsAndAreReported) {
  std::vector<SettingProblem> problems;
  Settings settings = Load(SettingScope::kGlobal,
                           "profiles:\n"
                           "  show_manager_at_startup: yes\n"
                           "  default:\n"
                           "  colour: red\n",
                           problems);
  ASSERT_EQ(problems.size(), 3u);
  EXPECT_EQ(problems[0].key, "profiles.show_manager_at_startup");
  EXPECT_EQ(problems[0].line, 2);
  EXPECT_EQ(problems[0].ToString(),
            "line 2, profiles.show_manager_at_startup: must be true or false "
            "(without quotes)");
  EXPECT_EQ(problems[1].message, "has no value");
  EXPECT_EQ(problems[2].key, "profiles.colour");
  EXPECT_EQ(problems[2].message, "unknown setting");
  EXPECT_EQ(settings, Settings(SettingScope::kGlobal));
}

TEST(SettingsTest, QuotedTrueIsNotABoolean) {
  std::vector<SettingProblem> problems;
  Load(SettingScope::kGlobal, "profiles:\n  show_manager_at_startup: 'true'\n",
       problems);
  EXPECT_EQ(problems.size(), 1u);
}

TEST(SettingsTest, ChoicesAndLimits) {
  std::vector<SettingProblem> problems;
  Settings settings =
      Load(SettingScope::kProfile,
           "profile:\n  color: magenta\n  name: " + std::string(65, 'x') + "\n",
           problems);
  ASSERT_EQ(problems.size(), 2u);
  EXPECT_NE(problems[0].message.find("grey, blue"), std::string::npos);
  EXPECT_EQ(settings.GetString("profile.color"), "blue");

  EXPECT_TRUE(settings.Set("profile.name", std::string("a\nb")));
  EXPECT_TRUE(settings.Set("profile.name", true));
  EXPECT_TRUE(settings.Set("profile.unknown", std::string("x")));
  EXPECT_FALSE(settings.Set("profile.name", std::string("Klient – Ä")));
  EXPECT_EQ(settings.GetString("profile.name"), "Klient – Ä");
}

TEST(SettingsTest, NotAMap) {
  std::vector<SettingProblem> problems;
  Load(SettingScope::kGlobal, "- a\n- b\n", problems);
  ASSERT_EQ(problems.size(), 1u);
  EXPECT_TRUE(problems[0].key.empty());
}

TEST(SettingsTest, Integers) {
  std::vector<SettingProblem> problems;
  Settings settings = Load(SettingScope::kGlobal,
                           "devtools:\n"
                           "  dock_size: 500\n"
                           "  remote_debugging_port: 80\n",
                           problems);
  EXPECT_EQ(settings.GetInt("devtools.dock_size"), 500);
  ASSERT_EQ(problems.size(), 1u);
  EXPECT_EQ(problems[0].message, "must be a whole number from 1024 to 65535");
  EXPECT_EQ(settings.GetInt("devtools.remote_debugging_port"), 9222);

  problems.clear();
  Load(SettingScope::kGlobal, "devtools:\n  dock_size: '500'\n", problems);
  Load(SettingScope::kGlobal, "devtools:\n  dock_size: 5.5\n", problems);
  EXPECT_EQ(problems.size(), 2u);
  EXPECT_TRUE(settings.Set("devtools.dock_size", std::int64_t{1}));
  EXPECT_FALSE(settings.Set("devtools.dock_size", std::int64_t{1000}));
  EXPECT_TRUE(settings.Set("devtools.dock_size", std::string("1000")));
}

TEST(SettingsTest, FileFormat) {
  Settings settings(SettingScope::kProfile);
  ASSERT_FALSE(settings.Set("profile.name", std::string("Work")));
  EXPECT_EQ(EmitYaml(settings.ToYaml()),
            "profile:\n"
            "  name: \"Work\"\n"
            "  color: blue\n"
            "history:\n"
            "  keep_list: \"\"\n");
  EXPECT_EQ(EmitYaml(Settings(SettingScope::kGlobal).ToYaml()),
            "appearance:\n"
            "  theme: system\n"
            "startup:\n"
            "  restore: last_session\n"
            "search:\n"
            "  engine: google\n"
            "  custom_template: \"\"\n"
            "suggestions:\n"
            "  history: true\n"
            "  bookmarks: true\n"
            "  search: true\n"
            "history:\n"
            "  keep: 90\n"
            "  view: site\n"
            "  never_record: \"\"\n"
            "bookmarks:\n"
            "  bar: new_tab_page\n"
            "downloads:\n"
            "  folder: \"\"\n"
            "  ask_where_to_save: false\n"
            "content:\n"
            "  spellcheck: true\n"
            "  json_viewer: true\n"
            "devtools:\n"
            "  toggle_position: right\n"
            "  dock_position: bottom\n"
            "  dock_size: 320\n"
            "  secret_headers: \"Authorization, Cookie, x-api-key, api-key, "
            "x-auth-token\"\n"
            "  remote_debugging: false\n"
            "  remote_debugging_port: 9222\n"
            "profiles:\n"
            "  default: \"default\"\n"
            "  show_manager_at_startup: false\n"
            "shortcuts:\n"
            "  new_tab: \"cmd+t\"\n"
            "  new_window: \"cmd+n\"\n"
            "  open_location: \"cmd+l\"\n"
            "  print: \"cmd+p\"\n"
            "  close_window: \"cmd+shift+w\"\n"
            "  close_tab: \"cmd+w\"\n"
            "  settings: \"cmd+comma\"\n"
            "  find: \"cmd+f\"\n"
            "  find_next: \"cmd+g\"\n"
            "  find_previous: \"cmd+shift+g\"\n"
            "  command_palette: \"cmd+k\"\n"
            "  stop: \"cmd+.\"\n"
            "  reload: \"cmd+r\"\n"
            "  force_reload: \"cmd+shift+r\"\n"
            "  zoom_reset: \"cmd+0\"\n"
            "  zoom_in: \"cmd+plus, cmd+=\"\n"
            "  zoom_out: \"cmd+-\"\n"
            "  full_screen: \"ctrl+cmd+f\"\n"
            "  devtools: \"opt+cmd+i\"\n"
            "  devtools_console: \"opt+cmd+j\"\n"
            "  devtools_inspect: \"opt+cmd+c\"\n"
            "  back: \"cmd+[\"\n"
            "  forward: \"cmd+]\"\n"
            "  home: \"shift+cmd+h\"\n"
            "  reopen_closed_tab: \"shift+cmd+t\"\n"
            "  show_history: \"cmd+y\"\n"
            "  clear_browsing_data: \"shift+cmd+delete\"\n"
            "  bookmark_page: \"cmd+d\"\n"
            "  bookmark_all_tabs: \"shift+cmd+d\"\n"
            "  bookmark_manager: \"opt+cmd+b\"\n"
            "  toggle_bookmarks_bar: \"shift+cmd+b\"\n"
            "  next_tab: \"opt+cmd+right, cmd+}, ctrl+tab\"\n"
            "  previous_tab: \"opt+cmd+left, cmd+{, ctrl+shift+tab\"\n"
            "  select_tab_1: \"cmd+1\"\n"
            "  select_tab_2: \"cmd+2\"\n"
            "  select_tab_3: \"cmd+3\"\n"
            "  select_tab_4: \"cmd+4\"\n"
            "  select_tab_5: \"cmd+5\"\n"
            "  select_tab_6: \"cmd+6\"\n"
            "  select_tab_7: \"cmd+7\"\n"
            "  select_tab_8: \"cmd+8\"\n"
            "  select_last_tab: \"cmd+9\"\n"
            "  new_tab_right: \"\"\n"
            "  duplicate_tab: \"\"\n"
            "  pin_tab: \"\"\n"
            "  mute_tab: \"\"\n"
            "  close_other_tabs: \"\"\n"
            "  close_tabs_left: \"\"\n"
            "  close_tabs_right: \"\"\n"
            "  site_info: \"\"\n"
            "  manage_profiles: \"\"\n");
}

TEST(SettingsTest, WritesAndReadsBack) {
  Settings settings(SettingScope::kProfile);
  ASSERT_FALSE(settings.Set("profile.name", std::string("no")));
  ASSERT_FALSE(settings.Set("profile.color", std::string("red")));
  std::string yaml = EmitYaml(settings.ToYaml());
  std::vector<SettingProblem> problems;
  Settings back = Load(SettingScope::kProfile, yaml, problems);
  EXPECT_TRUE(problems.empty()) << yaml;
  EXPECT_EQ(back, settings);
}

}  // namespace
}  // namespace brouk
