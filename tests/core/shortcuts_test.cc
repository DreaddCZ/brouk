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
        ParseShortcutList(action.defaults, KeyPlatform::kMac);
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

constexpr KeyPlatform kWin = KeyPlatform::kWindows;

std::string WindowsCanonical(std::string_view text) {
  std::optional<Shortcut> shortcut = ParseShortcut(text, kWin);
  return shortcut ? FormatShortcut(*shortcut, kWin) : "invalid";
}

TEST(ShortcutsTest, WindowsShortcutsNeedCtrlOrAlt) {
  EXPECT_EQ(WindowsCanonical("Shift+Ctrl+T"), "ctrl+shift+t");
  EXPECT_EQ(WindowsCanonical("alt+d"), "alt+d");
  EXPECT_EQ(WindowsCanonical("opt+left"), "alt+left");
  EXPECT_EQ(WindowsCanonical("f6"), "f6");
  EXPECT_EQ(WindowsCanonical("shift+f3"), "shift+f3");
  EXPECT_EQ(WindowsCanonical("shift+escape"), "shift+escape");
  EXPECT_EQ(WindowsCanonical("cmd+t"), "invalid");  // No Cmd on Windows.
  EXPECT_EQ(WindowsCanonical("shift+t"), "invalid");
  EXPECT_EQ(WindowsCanonical("t"), "invalid");
  // macOS still needs Cmd or Ctrl.
  EXPECT_FALSE(ParseShortcut("alt+left", KeyPlatform::kMac));
  EXPECT_FALSE(ParseShortcut("shift+escape", KeyPlatform::kMac));
}

TEST(ShortcutsTest, DisplaysLikeWindowsMenus) {
  auto display = [](std::string_view text) {
    return DisplayShortcut(*ParseShortcut(text, kWin), kWin);
  };
  EXPECT_EQ(display("ctrl+shift+t"), "Ctrl+Shift+T");
  EXPECT_EQ(display("ctrl+alt+shift+left"), "Ctrl+Alt+Shift+Left");
  EXPECT_EQ(display("ctrl+shift+delete"), "Ctrl+Shift+Del");
  EXPECT_EQ(display("shift+escape"), "Shift+Esc");
  EXPECT_EQ(display("ctrl+plus"), "Ctrl+Plus");
  EXPECT_EQ(display("ctrl+-"), "Ctrl+-");
  EXPECT_EQ(display("ctrl+pagedown"), "Ctrl+PgDn");
  EXPECT_EQ(display("f12"), "F12");
}

TEST(ShortcutsTest, WindowsDefaultsAreValidAndDoNotOverlap) {
  std::set<Shortcut> seen;
  for (const ShortcutAction& action : AllShortcutActions()) {
    std::optional<std::vector<Shortcut>> list =
        ParseShortcutList(action.windows_defaults, kWin);
    ASSERT_TRUE(list) << action.id;
    for (const Shortcut& shortcut : *list) {
      EXPECT_TRUE(seen.insert(shortcut).second)
          << FormatShortcut(shortcut, kWin) << " of " << action.id;
    }
    // Written as the settings file writes them, so that an unchanged
    // setting is recognised as the default.
    std::string written;
    for (const Shortcut& shortcut : *list) {
      written += (written.empty() ? "" : ", ") + FormatShortcut(shortcut, kWin);
    }
    EXPECT_EQ(written, action.windows_defaults) << action.id;
  }
}

TEST(ShortcutsTest, WindowsDefaultsWhenNothingChanged) {
  Settings global(SettingScope::kGlobal);
  // The settings hold the defaults of the platform the test runs on; set
  // Windows' (the settings accept only what is valid on this platform).
  for (const ShortcutAction& action : AllShortcutActions()) {
    global.Set(action.setting_key, std::string(action.windows_defaults));
  }
  std::vector<std::string> problems;
  ShortcutMap shortcuts = ResolveShortcuts(global, problems, kWin);
  EXPECT_TRUE(problems.empty());
  ASSERT_EQ(shortcuts["open_location"].size(), 3U);
  EXPECT_EQ(FormatShortcut(shortcuts["new_tab"][0], kWin), "ctrl+t");
  EXPECT_TRUE(shortcuts["settings"].empty());
}

TEST(ShortcutsTest, WindowsKeyPresses) {
  constexpr int kVkT = 'T';
  constexpr int kVkOemPlus = 0xBB;  // The "=" key of a US keyboard.
  constexpr int kVkOem6 = 0xDD;     // "]".
  EXPECT_EQ(WindowsKeyPress(kVkT, "t", true, false, true),
            (KeyPress{Shortcut::kControl | Shortcut::kShift, "t"}));
  // Letters and digits by key code, whatever the layout types.
  EXPECT_EQ(WindowsKeyPress('2', "\xC4\x9B", true, false, false).key, "2");
  EXPECT_EQ(WindowsKeyPress(0x62, "", true, false, false).key, "2");
  EXPECT_EQ(WindowsKeyPress(0x72, "", false, false, true),
            (KeyPress{Shortcut::kShift, "f3"}));
  EXPECT_EQ(WindowsKeyPress(0x2E, "", true, false, true).key, "delete");
  EXPECT_EQ(WindowsKeyPress(0x6B, "", true, false, false).key, "+");
  EXPECT_EQ(WindowsKeyPress(kVkOemPlus, "=", true, false, false).key, "=");
  EXPECT_EQ(WindowsKeyPress(kVkOemPlus, "+", true, false, true).key, "+");
  EXPECT_EQ(WindowsKeyPress(kVkOem6, "}", true, false, true).key, "}");
  EXPECT_EQ(WindowsKeyPress(0x10, "", false, false, true).key, "");  // Shift.
  EXPECT_EQ(WindowsKeyPress(0xBA, "\xC5\xAF", true, false, false).key, "");
}

TEST(ShortcutsTest, WindowsKeyPressesFindTheirAction) {
  Settings global(SettingScope::kGlobal);
  for (const ShortcutAction& action : AllShortcutActions()) {
    global.Set(action.setting_key, std::string(action.windows_defaults));
  }
  std::vector<std::string> problems;
  const ShortcutMap shortcuts = ResolveShortcuts(global, problems, kWin);
  auto action = [&shortcuts](int vk, std::string_view typed, bool control,
                             bool alt, bool shift) -> std::string {
    const ShortcutAction* found = ActionForKeyPress(
        shortcuts, WindowsKeyPress(vk, typed, control, alt, shift));
    return found ? std::string(found->id) : "none";
  };
  EXPECT_EQ(action('T', "t", true, false, false), "new_tab");
  EXPECT_EQ(action('T', "T", true, false, true), "reopen_closed_tab");
  EXPECT_EQ(action('D', "d", false, true, false), "open_location");
  EXPECT_EQ(action(0x25, "", false, true, false), "back");
  EXPECT_EQ(action(0x09, "", true, false, false), "next_tab");
  EXPECT_EQ(action(0x09, "", true, false, true), "previous_tab");
  EXPECT_EQ(action(0xBB, "=", true, false, false), "zoom_in");
  EXPECT_EQ(action(0xBB, "+", true, false, true), "zoom_in");  // Ctrl++.
  EXPECT_EQ(action(0x6B, "", true, false, false), "zoom_in");  // Number pad.
  EXPECT_EQ(action(0xBD, "-", true, false, false), "zoom_out");
  EXPECT_EQ(action(0x1B, "", false, false, true), "task_manager");
  EXPECT_EQ(action(0x72, "", false, false, true), "find_previous");
  EXPECT_EQ(action('9', "9", true, false, false), "select_last_tab");
  EXPECT_EQ(action('T', "t", false, false, false), "none");
  EXPECT_EQ(action(0x1B, "", false, false, false), "none");
}

}  // namespace
}  // namespace brouk
