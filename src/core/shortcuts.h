#ifndef BROUK_CORE_SHORTCUTS_H_
#define BROUK_CORE_SHORTCUTS_H_

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/settings.h"

namespace brouk {

// One key combination, e.g. Cmd+Shift+T.
struct Shortcut {
  enum Modifier : std::uint8_t {
    kCommand = 1,
    kShift = 2,
    kOption = 4,
    kControl = 8,
  };
  std::uint8_t modifiers = 0;
  // A lowercase letter, a digit or another character as the key types it
  // ("[", "}", "="), or a named key: left, right, up, down, tab, space,
  // return, escape, delete, home, end, pageup, pagedown, f1 ... f12, comma
  // and plus (the last two because "," and "+" separate shortcuts and keys
  // in the settings).
  std::string key;

  bool operator==(const Shortcut&) const = default;
  auto operator<=>(const Shortcut&) const = default;
};

// Reads one shortcut as written in the settings: modifiers and key joined by
// "+", e.g. "cmd+shift+t", "ctrl+tab", "cmd+opt+left", "cmd+comma". Order
// and case do not matter; "command", "option"/"alt" and "control" are
// accepted. A shortcut needs Cmd or Ctrl, except for F1 ... F12, so that it
// never takes a key needed for typing. nullopt if it is not valid.
std::optional<Shortcut> ParseShortcut(std::string_view text);

// Reads a setting's value: shortcuts separated by ",", or "" / "none" for no
// shortcut. nullopt if any of them is not valid.
std::optional<std::vector<Shortcut>> ParseShortcutList(std::string_view text);

// The canonical form for the settings file: "cmd+shift+t" (ctrl, opt,
// shift, cmd in this order).
std::string FormatShortcut(const Shortcut& shortcut);
// As macOS menus show it: "⌃⌥⇧⌘T", "⌘←", "⌘,".
std::string DisplayShortcut(const Shortcut& shortcut);

// An action that can have shortcuts (003: every toolbar action, remappable).
struct ShortcutAction {
  std::string_view id;           // e.g. "new_tab"
  std::string_view setting_key;  // "shortcuts.new_tab"
  std::string_view label;        // For the settings page and menus.
  // Chrome's shortcuts on macOS, as a setting value ("" for none).
  std::string_view defaults;
  // Chrome's reserved shortcuts: they work even when a page handles the key
  // (new tab, close tab, switching tabs…).
  bool reserved = false;
};

std::span<const ShortcutAction> AllShortcutActions();
// Null if there is no such action.
const ShortcutAction* FindShortcutAction(std::string_view id);

// The shortcuts in effect for each action id, from the global settings.
// A shortcut changed in the settings takes precedence over another action's
// default (that action loses it); when two changed settings use the same
// shortcut, the first action in the list keeps it and `problems` says so.
std::map<std::string, std::vector<Shortcut>, std::less<>> ResolveShortcuts(
    const Settings& global,
    std::vector<std::string>& problems);

}  // namespace brouk

#endif  // BROUK_CORE_SHORTCUTS_H_
