#include "core/shortcuts.h"

#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/settings.h"
#include "core/settings_schema.h"

namespace brouk {
namespace {

std::string Canonical(std::string_view text) {
  std::optional<Shortcut> shortcut = ParseShortcut(text);
  return shortcut ? FormatShortcut(*shortcut) : "invalid";
}

TEST(ShortcutsTest, ParsesModifiersInAnyOrderAndCase) {
  EXPECT_EQ(Canonical("cmd+shift+t"), "shift+cmd+t");
  EXPECT_EQ(Canonical("Shift + Command + T"), "shift+cmd+t");
  EXPECT_EQ(Canonical("alt+cmd+i"), "opt+cmd+i");
  EXPECT_EQ(Canonical("control+tab"), "ctrl+tab");
  EXPECT_EQ(Canonical("ctrl+opt+shift+cmd+left"), "ctrl+opt+shift+cmd+left");
  EXPECT_EQ(Canonical("cmd+}"), "cmd+}");
  EXPECT_EQ(Canonical("cmd+comma"), "cmd+comma");
  EXPECT_EQ(Canonical("cmd+-"), "cmd+-");
  EXPECT_EQ(Canonical("f5"), "f5");
}

TEST(ShortcutsTest, RejectsInvalidShortcuts) {
  EXPECT_EQ(Canonical(""), "invalid");
  EXPECT_EQ(Canonical("t"), "invalid");          // Needed for typing.
  EXPECT_EQ(Canonical("shift+t"), "invalid");    // Likewise.
  EXPECT_EQ(Canonical("opt+t"), "invalid");      // Types special characters.
  EXPECT_EQ(Canonical("cmd+cmd+t"), "invalid");  // Twice.
  EXPECT_EQ(Canonical("hyper+t"), "invalid");
  EXPECT_EQ(Canonical("cmd+"), "invalid");
  EXPECT_EQ(Canonical("cmd++"), "invalid");  // Written cmd+plus.
  EXPECT_EQ(Canonical("cmd+enter"), "invalid");
  EXPECT_EQ(Canonical("cmd+ä"), "invalid");
  EXPECT_EQ(Canonical("cmd+tt"), "invalid");
}

TEST(ShortcutsTest, ParsesLists) {
  std::optional<std::vector<Shortcut>> list =
      ParseShortcutList("opt+cmd+right, cmd+} ,ctrl+tab");
  ASSERT_TRUE(list);
  ASSERT_EQ(list->size(), 3U);
  EXPECT_EQ(FormatShortcut((*list)[1]), "cmd+}");
  EXPECT_TRUE(ParseShortcutList("")->empty());
  EXPECT_TRUE(ParseShortcutList(" None ")->empty());
  EXPECT_FALSE(ParseShortcutList("cmd+t, t"));
  EXPECT_FALSE(ParseShortcutList("cmd+t,"));
}

TEST(ShortcutsTest, DisplaysLikeMacMenus) {
  EXPECT_EQ(DisplayShortcut(*ParseShortcut("cmd+shift+t")), "⇧⌘T");
  EXPECT_EQ(DisplayShortcut(*ParseShortcut("ctrl+opt+cmd+left")), "⌃⌥⌘←");
  EXPECT_EQ(DisplayShortcut(*ParseShortcut("cmd+comma")), "⌘,");
  EXPECT_EQ(DisplayShortcut(*ParseShortcut("ctrl+tab")), "⌃⇥");
  EXPECT_EQ(DisplayShortcut(*ParseShortcut("f12")), "F12");
}

TEST(ShortcutsTest, DefaultsAreValidAndDoNotOverlap) {
  std::set<Shortcut> seen;
  for (const ShortcutAction& action : AllShortcutActions()) {
    std::optional<std::vector<Shortcut>> list =
        ParseShortcutList(action.defaults);
    ASSERT_TRUE(list) << action.id;
    for (const Shortcut& shortcut : *list) {
      EXPECT_TRUE(seen.insert(shortcut).second)
          << FormatShortcut(shortcut) << " of " << action.id;
    }
    EXPECT_EQ(action.setting_key, "shortcuts." + std::string(action.id));
    const SettingDef* def =
        FindSettingDef(SettingScope::kGlobal, action.setting_key);
    ASSERT_NE(def, nullptr) << action.id;
    EXPECT_EQ(def->format, SettingDef::Format::kShortcut);
  }
}

TEST(ShortcutsTest, SettingsRejectInvalidShortcuts) {
  Settings global(SettingScope::kGlobal);
  EXPECT_TRUE(global.Set("shortcuts.new_tab", std::string("t")));
  EXPECT_EQ(global.GetString("shortcuts.new_tab"), "cmd+t");
  EXPECT_FALSE(global.Set("shortcuts.new_tab", std::string("cmd+opt+t")));
}

TEST(ShortcutsTest, DefaultsWhenNothingChanged) {
  Settings global(SettingScope::kGlobal);
  std::vector<std::string> problems;
  auto shortcuts = ResolveShortcuts(global, problems);
  EXPECT_TRUE(problems.empty());
  ASSERT_EQ(shortcuts["next_tab"].size(), 3U);
  EXPECT_EQ(FormatShortcut(shortcuts["new_tab"][0]), "cmd+t");
  EXPECT_TRUE(shortcuts["pin_tab"].empty());
}

TEST(ShortcutsTest, ChangedShortcutTakesItFromADefault) {
  Settings global(SettingScope::kGlobal);
  global.Set("shortcuts.pin_tab", std::string("cmd+p"));
  std::vector<std::string> problems;
  auto shortcuts = ResolveShortcuts(global, problems);
  EXPECT_TRUE(problems.empty());
  EXPECT_EQ(FormatShortcut(shortcuts["pin_tab"][0]), "cmd+p");
  EXPECT_TRUE(shortcuts["print"].empty());
}

TEST(ShortcutsTest, TwoChangedSettingsWithTheSameShortcut) {
  Settings global(SettingScope::kGlobal);
  global.Set("shortcuts.reload", std::string("cmd+e"));
  global.Set("shortcuts.pin_tab", std::string("cmd+e, cmd+shift+e"));
  std::vector<std::string> problems;
  auto shortcuts = ResolveShortcuts(global, problems);
  ASSERT_EQ(problems.size(), 1U);
  EXPECT_EQ(problems[0],
            "⌘E is set for both \"Reload\" and \"Pin or unpin tab\"; only "
            "\"Reload\" uses it.");
  EXPECT_EQ(shortcuts["reload"].size(), 1U);
  ASSERT_EQ(shortcuts["pin_tab"].size(), 1U);
  EXPECT_EQ(FormatShortcut(shortcuts["pin_tab"][0]), "shift+cmd+e");
}

TEST(ShortcutsTest, RemovingAShortcut) {
  Settings global(SettingScope::kGlobal);
  global.Set("shortcuts.print", std::string("none"));
  std::vector<std::string> problems;
  EXPECT_TRUE(ResolveShortcuts(global, problems)["print"].empty());
}

}  // namespace
}  // namespace brouk
