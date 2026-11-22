#include "core/shortcuts.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace brouk {

namespace {

// Chrome's macOS and Windows shortcuts (menus and its accelerator tables).
// Standard items (Quit, Hide, Minimize, Undo, Copy, Paste…) are not listed:
// they are not browser actions, and macOS can remap them already. Chrome on
// Windows has no shortcut for Settings, Stop (Esc reaches the page) or
// Passwords.
constexpr std::array<ShortcutAction, 57> kActions = {{
    // File.
    {"new_tab", "shortcuts.new_tab", "New tab", "cmd+t", "ctrl+t", true},
    {"new_window", "shortcuts.new_window", "New window", "cmd+n", "ctrl+n",
     true},
    {"open_location", "shortcuts.open_location",
     "Go to the address bar (Open Location)", "cmd+l", "ctrl+l, alt+d, f6"},
    {"print", "shortcuts.print", "Print", "cmd+p", "ctrl+p"},
    {"close_window", "shortcuts.close_window", "Close window", "cmd+shift+w",
     "ctrl+shift+w", true},
    {"close_tab", "shortcuts.close_tab", "Close tab", "cmd+w",
     "ctrl+w, ctrl+f4", true},
    {"settings", "shortcuts.settings", "Settings", "cmd+comma", ""},
    // Edit > Find.
    {"find", "shortcuts.find", "Find", "cmd+f", "ctrl+f"},
    {"find_next", "shortcuts.find_next", "Find next", "cmd+g", "f3, ctrl+g"},
    {"find_previous", "shortcuts.find_previous", "Find previous", "cmd+shift+g",
     "shift+f3, ctrl+shift+g"},
    // View.
    {"command_palette", "shortcuts.command_palette", "Command palette", "cmd+k",
     "ctrl+k"},
    {"stop", "shortcuts.stop", "Stop loading", "cmd+.", ""},
    {"reload", "shortcuts.reload", "Reload", "cmd+r", "f5, ctrl+r"},
    {"force_reload", "shortcuts.force_reload", "Reload without the cache",
     "cmd+shift+r", "ctrl+shift+r, shift+f5, ctrl+f5"},
    {"zoom_reset", "shortcuts.zoom_reset", "Actual size", "cmd+0", "ctrl+0"},
    {"zoom_in", "shortcuts.zoom_in", "Zoom in",
     "cmd+plus, cmd+=", "ctrl+plus, ctrl+="},
    {"zoom_out", "shortcuts.zoom_out", "Zoom out", "cmd+-", "ctrl+-"},
    {"full_screen", "shortcuts.full_screen", "Full screen", "ctrl+cmd+f",
     "f11"},
    // View > Developer (006).
    {"devtools", "shortcuts.devtools", "Developer tools on/off", "opt+cmd+i",
     "f12, ctrl+shift+i"},
    {"devtools_console", "shortcuts.devtools_console", "JavaScript console",
     "opt+cmd+j", "ctrl+shift+j"},
    {"devtools_inspect", "shortcuts.devtools_inspect", "Inspect elements",
     "opt+cmd+c", "ctrl+shift+c"},
    // The dev tools' Utilities (027); no shortcut by default.
    {"devtools_utilities", "shortcuts.devtools_utilities", "Utilities", "", ""},
    // History.
    {"back", "shortcuts.back", "Back", "cmd+[", "alt+left"},
    {"forward", "shortcuts.forward", "Forward", "cmd+]", "alt+right"},
    {"home", "shortcuts.home", "Home (new tab page)", "shift+cmd+h",
     "alt+home"},
    {"reopen_closed_tab", "shortcuts.reopen_closed_tab", "Reopen closed tab",
     "shift+cmd+t", "ctrl+shift+t"},
    {"show_history", "shortcuts.show_history", "Show full history", "cmd+y",
     "ctrl+h"},
    {"clear_browsing_data", "shortcuts.clear_browsing_data",
     "Clear browsing data", "shift+cmd+delete", "ctrl+shift+delete"},
    // Bookmarks (011).
    {"bookmark_page", "shortcuts.bookmark_page", "Bookmark this tab", "cmd+d",
     "ctrl+d"},
    {"bookmark_all_tabs", "shortcuts.bookmark_all_tabs", "Bookmark all tabs",
     "shift+cmd+d", "ctrl+shift+d"},
    {"bookmark_manager", "shortcuts.bookmark_manager", "Bookmark manager",
     "opt+cmd+b", "ctrl+shift+o"},
    {"toggle_bookmarks_bar", "shortcuts.toggle_bookmarks_bar",
     "Always show bookmarks bar", "shift+cmd+b", "ctrl+shift+b"},
    // Passwords (008).
    {"passwords", "shortcuts.passwords", "Passwords", "opt+cmd+p", ""},
    // Downloads (014).
    {"show_downloads", "shortcuts.show_downloads", "Downloads", "opt+cmd+l",
     "ctrl+j"},
    // Task manager (020); Chrome has Shift+Esc on Windows and Linux.
    {"task_manager", "shortcuts.task_manager", "Task manager",
     "shift+cmd+escape", "shift+escape"},
    // Tab.
    {"next_tab", "shortcuts.next_tab", "Next tab",
     "opt+cmd+right, cmd+}, ctrl+tab", "ctrl+tab, ctrl+pagedown", true},
    {"previous_tab", "shortcuts.previous_tab", "Previous tab",
     "opt+cmd+left, cmd+{, ctrl+shift+tab", "ctrl+shift+tab, ctrl+pageup",
     true},
    {"select_tab_1", "shortcuts.select_tab_1", "Tab 1", "cmd+1", "ctrl+1"},
    {"select_tab_2", "shortcuts.select_tab_2", "Tab 2", "cmd+2", "ctrl+2"},
    {"select_tab_3", "shortcuts.select_tab_3", "Tab 3", "cmd+3", "ctrl+3"},
    {"select_tab_4", "shortcuts.select_tab_4", "Tab 4", "cmd+4", "ctrl+4"},
    {"select_tab_5", "shortcuts.select_tab_5", "Tab 5", "cmd+5", "ctrl+5"},
    {"select_tab_6", "shortcuts.select_tab_6", "Tab 6", "cmd+6", "ctrl+6"},
    {"select_tab_7", "shortcuts.select_tab_7", "Tab 7", "cmd+7", "ctrl+7"},
    {"select_tab_8", "shortcuts.select_tab_8", "Tab 8", "cmd+8", "ctrl+8"},
    {"select_last_tab", "shortcuts.select_last_tab", "Last tab", "cmd+9",
     "ctrl+9"},
    {"new_tab_right", "shortcuts.new_tab_right", "New tab to the right", "",
     ""},
    {"duplicate_tab", "shortcuts.duplicate_tab", "Duplicate tab", "", ""},
    {"move_tab_new_window", "shortcuts.move_tab_new_window",
     "Move tab to new window", "", ""},
    {"pin_tab", "shortcuts.pin_tab", "Pin or unpin tab", "", ""},
    {"mute_tab", "shortcuts.mute_tab", "Mute or unmute tab", "", ""},
    {"close_other_tabs", "shortcuts.close_other_tabs", "Close other tabs", "",
     ""},
    {"close_tabs_left", "shortcuts.close_tabs_left", "Close tabs to the left",
     "", ""},
    {"close_tabs_right", "shortcuts.close_tabs_right",
     "Close tabs to the right", "", ""},
    // Toolbar.
    {"site_info", "shortcuts.site_info", "Site information", "", ""},
    {"manage_profiles", "shortcuts.manage_profiles", "Manage profiles", "", ""},
    // Help (016).
    {"help", "shortcuts.help", "Brouk help", "shift+cmd+?", "f1"},
}};

constexpr std::array<std::string_view, 27> kNamedKeys = {
    "left",   "right",  "up",   "down", "tab",    "space",    "return",
    "escape", "delete", "home", "end",  "pageup", "pagedown", "comma",
    "plus",   "f1",     "f2",   "f3",   "f4",     "f5",       "f6",
    "f7",     "f8",     "f9",   "f10",  "f11",    "f12"};

std::string Lower(std::string_view text) {
  std::string result(text);
  for (char& c : result) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return result;
}

std::string_view Trim(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
    text.remove_prefix(1);
  }
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
    text.remove_suffix(1);
  }
  return text;
}

bool IsNamedKey(std::string_view key) {
  for (std::string_view name : kNamedKeys) {
    if (name == key) {
      return true;
    }
  }
  return false;
}

bool IsFunctionKey(std::string_view key) {
  return key.size() >= 2 && key[0] == 'f' &&
         std::isdigit(static_cast<unsigned char>(key[1]));
}

std::optional<std::uint8_t> ModifierFromName(std::string_view name) {
  if (name == "cmd" || name == "command") {
    return Shortcut::kCommand;
  }
  if (name == "shift") {
    return Shortcut::kShift;
  }
  if (name == "opt" || name == "option" || name == "alt") {
    return Shortcut::kOption;
  }
  if (name == "ctrl" || name == "control") {
    return Shortcut::kControl;
  }
  return std::nullopt;
}

std::vector<std::string_view> Split(std::string_view text, char separator) {
  std::vector<std::string_view> parts;
  std::size_t start = 0;
  while (true) {
    std::size_t end = text.find(separator, start);
    if (end == std::string_view::npos) {
      parts.push_back(text.substr(start));
      return parts;
    }
    parts.push_back(text.substr(start, end - start));
    start = end + 1;
  }
}

// Windows' menus write shortcuts as words joined by "+": "Ctrl+Shift+T".
std::string DisplayWindowsShortcut(const Shortcut& shortcut) {
  std::string text;
  if ((shortcut.modifiers & Shortcut::kControl) != 0) {
    text += "Ctrl+";
  }
  if ((shortcut.modifiers & Shortcut::kOption) != 0) {
    text += "Alt+";
  }
  if ((shortcut.modifiers & Shortcut::kShift) != 0) {
    text += "Shift+";
  }
  static const std::map<std::string_view, std::string_view> kNames = {
      {"left", "Left"},     {"right", "Right"}, {"up", "Up"},
      {"down", "Down"},     {"tab", "Tab"},     {"space", "Space"},
      {"return", "Enter"},  {"escape", "Esc"},  {"delete", "Del"},
      {"home", "Home"},     {"end", "End"},     {"pageup", "PgUp"},
      {"pagedown", "PgDn"}, {"comma", ","},     {"plus", "Plus"}};
  auto name = kNames.find(shortcut.key);
  if (name != kNames.end()) {
    return text + std::string(name->second);
  }
  std::string key = shortcut.key;
  for (char& c : key) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return text + key;
}

bool IsLetterOrDigit(std::string_view key) {
  return key.size() == 1 &&
         std::isalnum(static_cast<unsigned char>(key[0])) != 0;
}

}  // namespace

std::optional<Shortcut> ParseShortcut(std::string_view text,
                                      KeyPlatform platform) {
  std::vector<std::string_view> parts = Split(Trim(text), '+');
  Shortcut shortcut;
  for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
    std::optional<std::uint8_t> modifier =
        ModifierFromName(Lower(Trim(parts[i])));
    if (!modifier || (shortcut.modifiers & *modifier) != 0) {
      return std::nullopt;
    }
    shortcut.modifiers |= *modifier;
  }
  std::string_view raw_key = Trim(parts.back());
  if (raw_key.size() == 1) {
    auto c = static_cast<unsigned char>(raw_key[0]);
    // Printable ASCII; "," and "+" are written "comma" and "plus".
    if (c <= 0x20 || c >= 0x7F || c == ',' || c == '+') {
      return std::nullopt;
    }
    shortcut.key = Lower(raw_key);
  } else {
    shortcut.key = Lower(raw_key);
    if (!IsNamedKey(shortcut.key)) {
      return std::nullopt;
    }
  }
  if (platform == KeyPlatform::kWindows) {
    // Windows has no Cmd (the Windows key belongs to the system).
    if ((shortcut.modifiers & Shortcut::kCommand) != 0) {
      return std::nullopt;
    }
    bool has_control_or_alt =
        (shortcut.modifiers & (Shortcut::kControl | Shortcut::kOption)) != 0;
    if (!has_control_or_alt && !IsFunctionKey(shortcut.key) &&
        shortcut.key != "escape") {
      return std::nullopt;
    }
    return shortcut;
  }
  bool has_command_or_control =
      (shortcut.modifiers & (Shortcut::kCommand | Shortcut::kControl)) != 0;
  if (!has_command_or_control && !IsFunctionKey(shortcut.key)) {
    return std::nullopt;
  }
  return shortcut;
}

std::optional<std::vector<Shortcut>> ParseShortcutList(std::string_view text,
                                                       KeyPlatform platform) {
  std::vector<Shortcut> shortcuts;
  std::string_view trimmed = Trim(text);
  if (trimmed.empty() || Lower(trimmed) == "none") {
    return shortcuts;
  }
  for (std::string_view part : Split(trimmed, ',')) {
    std::optional<Shortcut> shortcut = ParseShortcut(part, platform);
    if (!shortcut) {
      return std::nullopt;
    }
    shortcuts.push_back(std::move(*shortcut));
  }
  return shortcuts;
}

std::string FormatShortcut(const Shortcut& shortcut, KeyPlatform platform) {
  std::string text;
  if ((shortcut.modifiers & Shortcut::kControl) != 0) {
    text += "ctrl+";
  }
  if ((shortcut.modifiers & Shortcut::kOption) != 0) {
    text += platform == KeyPlatform::kWindows ? "alt+" : "opt+";
  }
  if ((shortcut.modifiers & Shortcut::kShift) != 0) {
    text += "shift+";
  }
  if ((shortcut.modifiers & Shortcut::kCommand) != 0) {
    text += "cmd+";
  }
  return text + shortcut.key;
}

std::string DisplayShortcut(const Shortcut& shortcut, KeyPlatform platform) {
  if (platform == KeyPlatform::kWindows) {
    return DisplayWindowsShortcut(shortcut);
  }
  std::string text;
  if ((shortcut.modifiers & Shortcut::kControl) != 0) {
    text += "⌃";
  }
  if ((shortcut.modifiers & Shortcut::kOption) != 0) {
    text += "⌥";
  }
  if ((shortcut.modifiers & Shortcut::kShift) != 0) {
    text += "⇧";
  }
  if ((shortcut.modifiers & Shortcut::kCommand) != 0) {
    text += "⌘";
  }
  static const std::map<std::string_view, std::string_view> kSymbols = {
      {"left", "←"},     {"right", "→"},     {"up", "↑"},     {"down", "↓"},
      {"tab", "⇥"},      {"space", "Space"}, {"return", "↩"}, {"escape", "⎋"},
      {"delete", "⌫"},   {"home", "↖"},      {"end", "↘"},    {"pageup", "⇞"},
      {"pagedown", "⇟"}, {"comma", ","},     {"plus", "+"}};
  auto symbol = kSymbols.find(shortcut.key);
  if (symbol != kSymbols.end()) {
    return text + std::string(symbol->second);
  }
  std::string key = shortcut.key;
  for (char& c : key) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return text + key;
}

std::span<const ShortcutAction> AllShortcutActions() {
  return kActions;
}

std::string_view DefaultShortcuts(const ShortcutAction& action,
                                  KeyPlatform platform) {
  return platform == KeyPlatform::kWindows ? action.windows_defaults
                                           : action.defaults;
}

const ShortcutAction* FindShortcutAction(std::string_view id) {
  for (const ShortcutAction& action : kActions) {
    if (action.id == id) {
      return &action;
    }
  }
  return nullptr;
}

ShortcutMap ResolveShortcuts(const Settings& global,
                             std::vector<std::string>& problems,
                             KeyPlatform platform) {
  ShortcutMap result;
  // Shortcut -> label of the changed setting that uses it.
  std::map<Shortcut, std::string_view> changed;
  std::vector<const ShortcutAction*> unchanged;
  for (const ShortcutAction& action : kActions) {
    const std::string& value = global.GetString(action.setting_key);
    std::optional<std::vector<Shortcut>> shortcuts =
        ParseShortcutList(value, platform);
    if (value == DefaultShortcuts(action, platform) || !shortcuts) {
      unchanged.push_back(&action);
      continue;
    }
    std::vector<Shortcut>& kept = result[std::string(action.id)];
    for (const Shortcut& shortcut : *shortcuts) {
      auto [it, added] = changed.emplace(shortcut, action.label);
      if (added) {
        kept.push_back(shortcut);
      } else if (it->second != action.label) {
        problems.push_back(
            DisplayShortcut(shortcut, platform) + " is set for both \"" +
            std::string(it->second) + "\" and \"" + std::string(action.label) +
            "\"; only \"" + std::string(it->second) + "\" uses it.");
      }
    }
  }
  for (const ShortcutAction* action : unchanged) {
    std::vector<Shortcut>& kept = result[std::string(action->id)];
    std::vector<Shortcut> defaults =
        ParseShortcutList(DefaultShortcuts(*action, platform), platform)
            .value_or(std::vector<Shortcut>{});
    for (const Shortcut& shortcut : defaults) {
      if (!changed.contains(shortcut)) {
        kept.push_back(shortcut);
      }
    }
  }
  return result;
}

KeyPress WindowsKeyPress(int virtual_key,
                         std::string_view typed,
                         bool control,
                         bool alt,
                         bool shift) {
  // Windows' virtual key codes (WinUser.h), so that this stays portable.
  static const std::map<int, std::string_view> kNamed = {
      {0x09, "tab"},
      {0x0D, "return"},
      {0x1B, "escape"},
      {0x20, "space"},
      {0x21, "pageup"},
      {0x22, "pagedown"},
      {0x23, "end"},
      {0x24, "home"},
      {0x25, "left"},
      {0x26, "up"},
      {0x27, "right"},
      {0x28, "down"},
      {0x2E, "delete"},
      // The number pad's operators.
      {0x6A, "*"},
      {0x6B, "+"},
      {0x6D, "-"},
      {0x6E, "."},
      {0x6F, "/"}};
  KeyPress press;
  if (control) {
    press.modifiers |= Shortcut::kControl;
  }
  if (alt) {
    press.modifiers |= Shortcut::kOption;
  }
  if (shift) {
    press.modifiers |= Shortcut::kShift;
  }
  if (auto named = kNamed.find(virtual_key); named != kNamed.end()) {
    press.key = std::string(named->second);
  } else if (virtual_key >= 'A' && virtual_key <= 'Z') {
    press.key = std::string(1, static_cast<char>(virtual_key - 'A' + 'a'));
  } else if (virtual_key >= '0' && virtual_key <= '9') {
    press.key = std::string(1, static_cast<char>(virtual_key));
  } else if (virtual_key >= 0x60 && virtual_key <= 0x69) {  // Number pad.
    press.key = std::string(1, static_cast<char>('0' + virtual_key - 0x60));
  } else if (virtual_key >= 0x70 && virtual_key <= 0x7B) {  // F1 ... F12.
    press.key = "f" + std::to_string(virtual_key - 0x70 + 1);
  } else if (typed.size() == 1 && typed[0] > ' ' && typed[0] <= '~') {
    press.key = Lower(typed);
  }
  return press;
}

bool KeyPressMatches(const KeyPress& press, const Shortcut& shortcut) {
  if (press.key.empty()) {
    return false;
  }
  std::string_view key = shortcut.key;
  if (key == "comma") {
    key = ",";
  } else if (key == "plus") {
    key = "+";
  }
  if (press.key != key) {
    return false;
  }
  if (press.modifiers == shortcut.modifiers) {
    return true;
  }
  // "}" or "+" typed with Shift matches a shortcut written without it.
  const bool character = !IsLetterOrDigit(key) && !IsNamedKey(key);
  return character &&
         press.modifiers == (shortcut.modifiers | Shortcut::kShift);
}

const ShortcutAction* ActionForKeyPress(const ShortcutMap& shortcuts,
                                        const KeyPress& press) {
  for (const ShortcutAction& action : kActions) {
    auto it = shortcuts.find(action.id);
    if (it == shortcuts.end()) {
      continue;
    }
    for (const Shortcut& shortcut : it->second) {
      if (KeyPressMatches(press, shortcut)) {
        return &action;
      }
    }
  }
  return nullptr;
}

}  // namespace brouk
