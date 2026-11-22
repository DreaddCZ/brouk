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

// Shortcuts differ by platform (003, 024): macOS has Cmd and Chrome's macOS
// defaults, Windows has Ctrl and Alt and Chrome's Windows defaults. The
// functions below take the platform so that both can be tested anywhere;
// the app uses the one it runs on.
enum class KeyPlatform : std::uint8_t { kMac, kWindows };
#if defined(_WIN32)
inline constexpr KeyPlatform kThisKeyPlatform = KeyPlatform::kWindows;
#else
inline constexpr KeyPlatform kThisKeyPlatform = KeyPlatform::kMac;
#endif

// One key combination, e.g. Cmd+Shift+T.
struct Shortcut {
  enum Modifier : std::uint8_t {
    kCommand = 1,
    kShift = 2,
    kOption = 4,  // Option on macOS, Alt on Windows.
    kControl = 8,
  };
  std::uint8_t modifiers = 0;
  // A lowercase letter, a digit or another character as the key types it
  // ("[", "}", "="), or a named key: left, right, up, down, tab, space,
  // return, escape, delete (macOS: ⌫, Windows: Del), home, end, pageup,
  // pagedown, f1 ... f12, comma and plus (the last two because "," and "+"
  // separate shortcuts and keys in the settings).
  std::string key;

  bool operator==(const Shortcut&) const = default;
  auto operator<=>(const Shortcut&) const = default;
};

// Reads one shortcut as written in the settings: modifiers and key joined by
// "+", e.g. "cmd+shift+t", "ctrl+tab", "cmd+opt+left", "cmd+comma". Order
// and case do not matter; "command", "option"/"alt" and "control" are
// accepted. So that a shortcut never takes a key needed for typing, it needs
// Cmd or Ctrl on macOS (Option types special characters there), and Ctrl or
// Alt on Windows, which has no Cmd; F1 ... F12 need nothing, and on Windows
// neither does Esc (Shift+Esc, Chrome's task manager). nullopt if it is not
// valid.
std::optional<Shortcut> ParseShortcut(std::string_view text,
                                      KeyPlatform platform = kThisKeyPlatform);

// Reads a setting's value: shortcuts separated by ",", or "" / "none" for no
// shortcut. nullopt if any of them is not valid.
std::optional<std::vector<Shortcut>> ParseShortcutList(
    std::string_view text,
    KeyPlatform platform = kThisKeyPlatform);

// The canonical form for the settings file: "shift+cmd+t" on macOS (ctrl,
// opt, shift, cmd in this order), "ctrl+alt+shift+t" on Windows.
std::string FormatShortcut(const Shortcut& shortcut,
                           KeyPlatform platform = kThisKeyPlatform);
// As the platform's menus show it: "⌃⌥⇧⌘T", "⌘←", "⌘," on macOS,
// "Ctrl+Alt+Shift+T", "Alt+Left", "Ctrl+," on Windows.
std::string DisplayShortcut(const Shortcut& shortcut,
                            KeyPlatform platform = kThisKeyPlatform);

// An action that can have shortcuts (003: every toolbar action, remappable).
struct ShortcutAction {
  std::string_view id;           // e.g. "new_tab"
  std::string_view setting_key;  // "shortcuts.new_tab"
  std::string_view label;        // For the settings page and menus.
  // Chrome's shortcuts on macOS, as a setting value ("" for none).
  std::string_view defaults;
  // Chrome's shortcuts on Windows, as a setting value ("" for none).
  std::string_view windows_defaults;
  // Chrome's reserved shortcuts: they work even when a page handles the key
  // (new tab, close tab, switching tabs…).
  bool reserved = false;
};

std::span<const ShortcutAction> AllShortcutActions();
// Null if there is no such action.
const ShortcutAction* FindShortcutAction(std::string_view id);
// The action's default setting value on `platform`.
std::string_view DefaultShortcuts(const ShortcutAction& action,
                                  KeyPlatform platform = kThisKeyPlatform);

// The shortcuts in effect for each action id, from the global settings.
// A shortcut changed in the settings takes precedence over another action's
// default (that action loses it); when two changed settings use the same
// shortcut, the first action in the list keeps it and `problems` says so.
using ShortcutMap = std::map<std::string, std::vector<Shortcut>, std::less<>>;
ShortcutMap ResolveShortcuts(const Settings& global,
                             std::vector<std::string>& problems,
                             KeyPlatform platform = kThisKeyPlatform);

// A key pressed on Windows, reduced to what a shortcut compares: the
// modifiers held and the key, named ("left", "f5", "delete") or as typed.
struct KeyPress {
  std::uint8_t modifiers = 0;  // Shortcut::Modifier flags.
  std::string key;             // "" if no shortcut can use the key.

  bool operator==(const KeyPress&) const = default;
};

// The key press for Windows' virtual key code `virtual_key`. Letters and
// digits come from the key code, so they work in every keyboard layout (as
// Chrome's shortcuts do); other characters from `typed`, what the key types
// with Shift as held and without Ctrl or Alt ("}" for Shift+]).
KeyPress WindowsKeyPress(int virtual_key,
                         std::string_view typed,
                         bool control,
                         bool alt,
                         bool shift);

// Whether `press` is `shortcut`. A character typed with Shift ("}", "+")
// also matches a shortcut written without Shift, as on macOS.
bool KeyPressMatches(const KeyPress& press, const Shortcut& shortcut);

// The action whose shortcut `press` is, or null.
const ShortcutAction* ActionForKeyPress(const ShortcutMap& shortcuts,
                                        const KeyPress& press);

}  // namespace brouk

#endif  // BROUK_CORE_SHORTCUTS_H_
