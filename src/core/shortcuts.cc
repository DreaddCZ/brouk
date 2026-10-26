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

// Chrome's macOS shortcuts (menus and its accelerator table). Standard
// macOS items (Quit, Hide, Minimize, Undo, Copy, Paste…) are not listed:
// they are not browser actions and macOS can remap them already.
constexpr std::array<ShortcutAction, 54> kActions = {{
    // File.
    {"new_tab", "shortcuts.new_tab", "New tab", "cmd+t", true},
    {"new_window", "shortcuts.new_window", "New window", "cmd+n", true},
    {"open_location", "shortcuts.open_location",
     "Go to the address bar (Open Location)", "cmd+l"},
    {"print", "shortcuts.print", "Print", "cmd+p"},
    {"close_window", "shortcuts.close_window", "Close window", "cmd+shift+w",
     true},
    {"close_tab", "shortcuts.close_tab", "Close tab", "cmd+w", true},
    {"settings", "shortcuts.settings", "Settings", "cmd+comma"},
    // Edit > Find.
    {"find", "shortcuts.find", "Find", "cmd+f"},
    {"find_next", "shortcuts.find_next", "Find next", "cmd+g"},
    {"find_previous", "shortcuts.find_previous", "Find previous",
     "cmd+shift+g"},
    // View.
    {"command_palette", "shortcuts.command_palette", "Command palette",
     "cmd+k"},
    {"stop", "shortcuts.stop", "Stop loading", "cmd+."},
    {"reload", "shortcuts.reload", "Reload", "cmd+r"},
    {"force_reload", "shortcuts.force_reload", "Reload without the cache",
     "cmd+shift+r"},
    {"zoom_reset", "shortcuts.zoom_reset", "Actual size", "cmd+0"},
    {"zoom_in", "shortcuts.zoom_in", "Zoom in", "cmd+plus, cmd+="},
    {"zoom_out", "shortcuts.zoom_out", "Zoom out", "cmd+-"},
    {"full_screen", "shortcuts.full_screen", "Full screen", "ctrl+cmd+f"},
    // View > Developer (006).
    {"devtools", "shortcuts.devtools", "Developer tools on/off", "opt+cmd+i"},
    {"devtools_console", "shortcuts.devtools_console", "JavaScript console",
     "opt+cmd+j"},
    {"devtools_inspect", "shortcuts.devtools_inspect", "Inspect elements",
     "opt+cmd+c"},
    // History.
    {"back", "shortcuts.back", "Back", "cmd+["},
    {"forward", "shortcuts.forward", "Forward", "cmd+]"},
    {"home", "shortcuts.home", "Home (new tab page)", "shift+cmd+h"},
    {"reopen_closed_tab", "shortcuts.reopen_closed_tab", "Reopen closed tab",
     "shift+cmd+t"},
    {"show_history", "shortcuts.show_history", "Show full history", "cmd+y"},
    {"clear_browsing_data", "shortcuts.clear_browsing_data",
     "Clear browsing data", "shift+cmd+delete"},
    // Bookmarks (011).
    {"bookmark_page", "shortcuts.bookmark_page", "Bookmark this tab", "cmd+d"},
    {"bookmark_all_tabs", "shortcuts.bookmark_all_tabs", "Bookmark all tabs",
     "shift+cmd+d"},
    {"bookmark_manager", "shortcuts.bookmark_manager", "Bookmark manager",
     "opt+cmd+b"},
    {"toggle_bookmarks_bar", "shortcuts.toggle_bookmarks_bar",
     "Always show bookmarks bar", "shift+cmd+b"},
    // Passwords (008).
    {"passwords", "shortcuts.passwords", "Passwords", "opt+cmd+p"},
    // Downloads (014).
    {"show_downloads", "shortcuts.show_downloads", "Downloads", "opt+cmd+l"},
    // Tab.
    {"next_tab", "shortcuts.next_tab", "Next tab",
     "opt+cmd+right, cmd+}, ctrl+tab", true},
    {"previous_tab", "shortcuts.previous_tab", "Previous tab",
     "opt+cmd+left, cmd+{, ctrl+shift+tab", true},
    {"select_tab_1", "shortcuts.select_tab_1", "Tab 1", "cmd+1"},
    {"select_tab_2", "shortcuts.select_tab_2", "Tab 2", "cmd+2"},
    {"select_tab_3", "shortcuts.select_tab_3", "Tab 3", "cmd+3"},
    {"select_tab_4", "shortcuts.select_tab_4", "Tab 4", "cmd+4"},
    {"select_tab_5", "shortcuts.select_tab_5", "Tab 5", "cmd+5"},
    {"select_tab_6", "shortcuts.select_tab_6", "Tab 6", "cmd+6"},
    {"select_tab_7", "shortcuts.select_tab_7", "Tab 7", "cmd+7"},
    {"select_tab_8", "shortcuts.select_tab_8", "Tab 8", "cmd+8"},
    {"select_last_tab", "shortcuts.select_last_tab", "Last tab", "cmd+9"},
    {"new_tab_right", "shortcuts.new_tab_right", "New tab to the right", ""},
    {"duplicate_tab", "shortcuts.duplicate_tab", "Duplicate tab", ""},
    {"pin_tab", "shortcuts.pin_tab", "Pin or unpin tab", ""},
    {"mute_tab", "shortcuts.mute_tab", "Mute or unmute tab", ""},
    {"close_other_tabs", "shortcuts.close_other_tabs", "Close other tabs", ""},
    {"close_tabs_left", "shortcuts.close_tabs_left", "Close tabs to the left",
     ""},
    {"close_tabs_right", "shortcuts.close_tabs_right",
     "Close tabs to the right", ""},
    // Toolbar.
    {"site_info", "shortcuts.site_info", "Site information", ""},
    {"manage_profiles", "shortcuts.manage_profiles", "Manage profiles", ""},
    // Help (016).
    {"help", "shortcuts.help", "Brouk help", "shift+cmd+?"},
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

}  // namespace

std::optional<Shortcut> ParseShortcut(std::string_view text) {
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
  bool has_command_or_control =
      (shortcut.modifiers & (Shortcut::kCommand | Shortcut::kControl)) != 0;
  if (!has_command_or_control && !IsFunctionKey(shortcut.key)) {
    return std::nullopt;
  }
  return shortcut;
}

std::optional<std::vector<Shortcut>> ParseShortcutList(std::string_view text) {
  std::vector<Shortcut> shortcuts;
  std::string_view trimmed = Trim(text);
  if (trimmed.empty() || Lower(trimmed) == "none") {
    return shortcuts;
  }
  for (std::string_view part : Split(trimmed, ',')) {
    std::optional<Shortcut> shortcut = ParseShortcut(part);
    if (!shortcut) {
      return std::nullopt;
    }
    shortcuts.push_back(std::move(*shortcut));
  }
  return shortcuts;
}

std::string FormatShortcut(const Shortcut& shortcut) {
  std::string text;
  if ((shortcut.modifiers & Shortcut::kControl) != 0) {
    text += "ctrl+";
  }
  if ((shortcut.modifiers & Shortcut::kOption) != 0) {
    text += "opt+";
  }
  if ((shortcut.modifiers & Shortcut::kShift) != 0) {
    text += "shift+";
  }
  if ((shortcut.modifiers & Shortcut::kCommand) != 0) {
    text += "cmd+";
  }
  return text + shortcut.key;
}

std::string DisplayShortcut(const Shortcut& shortcut) {
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

const ShortcutAction* FindShortcutAction(std::string_view id) {
  for (const ShortcutAction& action : kActions) {
    if (action.id == id) {
      return &action;
    }
  }
  return nullptr;
}

std::map<std::string, std::vector<Shortcut>, std::less<>> ResolveShortcuts(
    const Settings& global,
    std::vector<std::string>& problems) {
  std::map<std::string, std::vector<Shortcut>, std::less<>> result;
  // Shortcut -> label of the changed setting that uses it.
  std::map<Shortcut, std::string_view> changed;
  std::vector<const ShortcutAction*> unchanged;
  for (const ShortcutAction& action : kActions) {
    const std::string& value = global.GetString(action.setting_key);
    std::optional<std::vector<Shortcut>> shortcuts = ParseShortcutList(value);
    if (value == action.defaults || !shortcuts) {
      unchanged.push_back(&action);
      continue;
    }
    std::vector<Shortcut>& kept = result[std::string(action.id)];
    for (const Shortcut& shortcut : *shortcuts) {
      auto [it, added] = changed.emplace(shortcut, action.label);
      if (added) {
        kept.push_back(shortcut);
      } else if (it->second != action.label) {
        problems.push_back(DisplayShortcut(shortcut) + " is set for both \"" +
                           std::string(it->second) + "\" and \"" +
                           std::string(action.label) + "\"; only \"" +
                           std::string(it->second) + "\" uses it.");
      }
    }
  }
  for (const ShortcutAction* action : unchanged) {
    std::vector<Shortcut>& kept = result[std::string(action->id)];
    std::vector<Shortcut> defaults =
        ParseShortcutList(action->defaults).value_or(std::vector<Shortcut>{});
    for (const Shortcut& shortcut : defaults) {
      if (!changed.contains(shortcut)) {
        kept.push_back(shortcut);
      }
    }
  }
  return result;
}

}  // namespace brouk
