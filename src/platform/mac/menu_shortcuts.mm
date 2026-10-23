#import "platform/mac/menu_shortcuts.h"

#import <Carbon/Carbon.h>  // kVK_* key codes.

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace brouk::mac {

NSNotificationName const kShortcutsChangedNotification =
    @"BRShortcutsChangedNotification";

namespace {

// Identifier prefix of the hidden copies that carry further shortcuts.
NSString* const kAlternatePrefix = @"alt.";

constexpr NSEventModifierFlags kRelevantModifiers =
    NSEventModifierFlagCommand | NSEventModifierFlagShift |
    NSEventModifierFlagOption | NSEventModifierFlagControl;

// Chrome's shortcuts until the settings are read.
std::map<std::string, std::vector<Shortcut>, std::less<>>& Current() {
  static auto* shortcuts = [] {
    std::vector<std::string> ignored;
    return new std::map<std::string, std::vector<Shortcut>, std::less<>>(
        ResolveShortcuts(Settings(SettingScope::kGlobal), ignored));
  }();
  return *shortcuts;
}

struct NamedKey {
  std::string_view name;
  unichar character;    // For menu key equivalents.
  unsigned short code;  // For matching key events.
};

constexpr std::array<NamedKey, 25> kNamedKeys = {{
    {"left", NSLeftArrowFunctionKey, kVK_LeftArrow},
    {"right", NSRightArrowFunctionKey, kVK_RightArrow},
    {"up", NSUpArrowFunctionKey, kVK_UpArrow},
    {"down", NSDownArrowFunctionKey, kVK_DownArrow},
    {"tab", '\t', kVK_Tab},
    {"space", ' ', kVK_Space},
    {"return", '\r', kVK_Return},
    {"escape", 0x1B, kVK_Escape},
    {"delete", NSBackspaceCharacter, kVK_Delete},
    {"home", NSHomeFunctionKey, kVK_Home},
    {"end", NSEndFunctionKey, kVK_End},
    {"pageup", NSPageUpFunctionKey, kVK_PageUp},
    {"pagedown", NSPageDownFunctionKey, kVK_PageDown},
    {"f1", NSF1FunctionKey, kVK_F1},
    {"f2", NSF2FunctionKey, kVK_F2},
    {"f3", NSF3FunctionKey, kVK_F3},
    {"f4", NSF4FunctionKey, kVK_F4},
    {"f5", NSF5FunctionKey, kVK_F5},
    {"f6", NSF6FunctionKey, kVK_F6},
    {"f7", NSF7FunctionKey, kVK_F7},
    {"f8", NSF8FunctionKey, kVK_F8},
    {"f9", NSF9FunctionKey, kVK_F9},
    {"f10", NSF10FunctionKey, kVK_F10},
    {"f11", NSF11FunctionKey, kVK_F11},
    {"f12", NSF12FunctionKey, kVK_F12},
}};

const NamedKey* FindNamedKey(std::string_view name) {
  for (const NamedKey& key : kNamedKeys) {
    if (key.name == name) {
      return &key;
    }
  }
  return nullptr;
}

NSEventModifierFlags MaskFor(const Shortcut& shortcut) {
  NSEventModifierFlags mask = 0;
  if ((shortcut.modifiers & Shortcut::kCommand) != 0) {
    mask |= NSEventModifierFlagCommand;
  }
  if ((shortcut.modifiers & Shortcut::kShift) != 0) {
    mask |= NSEventModifierFlagShift;
  }
  if ((shortcut.modifiers & Shortcut::kOption) != 0) {
    mask |= NSEventModifierFlagOption;
  }
  if ((shortcut.modifiers & Shortcut::kControl) != 0) {
    mask |= NSEventModifierFlagControl;
  }
  return mask;
}

NSString* KeyEquivalentFor(const Shortcut& shortcut) {
  if (shortcut.key == "comma") {
    return @",";
  }
  if (shortcut.key == "plus") {
    return @"+";
  }
  if (const NamedKey* named = FindNamedKey(shortcut.key)) {
    return [NSString stringWithCharacters:&named->character length:1];
  }
  return @(shortcut.key.c_str());
}

void SetKeyEquivalent(NSMenuItem* item, const Shortcut* shortcut) {
  item.keyEquivalent = shortcut ? KeyEquivalentFor(*shortcut) : @"";
  item.keyEquivalentModifierMask = shortcut ? MaskFor(*shortcut) : 0;
}

void RemoveAlternates(NSMenu* menu) {
  for (NSMenuItem* item in [menu.itemArray copy]) {
    if ([item.identifier hasPrefix:kAlternatePrefix]) {
      [menu removeItem:item];
    } else if (item.submenu) {
      RemoveAlternates(item.submenu);
    }
  }
}

void AddShortcuts(NSMenu* menu) {
  for (NSMenuItem* item in [menu.itemArray copy]) {
    if (item.submenu) {
      AddShortcuts(item.submenu);
    }
    if (item.identifier.length == 0 ||
        !FindShortcutAction(item.identifier.UTF8String)) {
      continue;
    }
    const std::vector<Shortcut>& shortcuts =
        ShortcutsFor(item.identifier.UTF8String);
    SetKeyEquivalent(item, shortcuts.empty() ? nullptr : &shortcuts[0]);
    NSInteger index = [menu indexOfItem:item];
    for (std::size_t i = 1; i < shortcuts.size(); ++i) {
      NSMenuItem* copy = [[NSMenuItem alloc] initWithTitle:item.title
                                                    action:item.action
                                             keyEquivalent:@""];
      copy.target = item.target;
      copy.tag = item.tag;
      copy.identifier =
          [kAlternatePrefix stringByAppendingString:item.identifier];
      SetKeyEquivalent(copy, &shortcuts[i]);
      copy.hidden = YES;
      copy.allowsKeyEquivalentWhenHidden = YES;
      [menu insertItem:copy atIndex:index + static_cast<NSInteger>(i)];
    }
  }
}

NSMenuItem* _Nullable FindItem(NSMenu* menu, NSString* identifier) {
  for (NSMenuItem* item in menu.itemArray) {
    if ([item.identifier isEqualToString:identifier]) {
      return item;
    }
    if (item.submenu) {
      if (NSMenuItem* found = FindItem(item.submenu, identifier)) {
        return found;
      }
    }
  }
  return nil;
}

bool IsLetterOrDigit(std::string_view key) {
  return key.size() == 1 &&
         ((key[0] >= 'a' && key[0] <= 'z') || (key[0] >= '0' && key[0] <= '9'));
}

}  // namespace

void SetShortcuts(
    std::map<std::string, std::vector<Shortcut>, std::less<>> shortcuts) {
  Current() = std::move(shortcuts);
  if (NSApp.mainMenu) {
    ApplyShortcutsToMenu(NSApp.mainMenu);
  }
  [NSNotificationCenter.defaultCenter
      postNotificationName:kShortcutsChangedNotification
                    object:nil];
}

const std::vector<Shortcut>& ShortcutsFor(std::string_view action) {
  static const std::vector<Shortcut> kNone;
  auto it = Current().find(action);
  return it == Current().end() ? kNone : it->second;
}

NSString* ToolTip(NSString* text, std::string_view action) {
  const std::vector<Shortcut>& shortcuts = ShortcutsFor(action);
  if (shortcuts.empty()) {
    return text;
  }
  return [NSString
      stringWithFormat:@"%@ (%s)", text, DisplayShortcut(shortcuts[0]).c_str()];
}

void ApplyShortcutsToMenu(NSMenu* menu) {
  RemoveAlternates(menu);
  AddShortcuts(menu);
}

bool EventMatchesShortcut(NSEvent* event, const Shortcut& shortcut) {
  if (event.type != NSEventTypeKeyDown) {
    return false;
  }
  NSEventModifierFlags flags = event.modifierFlags & kRelevantModifiers;
  NSEventModifierFlags mask = MaskFor(shortcut);
  if (shortcut.key != "comma" && shortcut.key != "plus") {
    if (const NamedKey* named = FindNamedKey(shortcut.key)) {
      return event.keyCode == named->code && flags == mask;
    }
  }
  std::string key = shortcut.key == "comma"  ? ","
                    : shortcut.key == "plus" ? "+"
                                             : shortcut.key;
  // Letters are matched by character, so keyboard layouts that move them
  // still work (as for Chrome's own shortcuts).
  NSString* typed = event.charactersIgnoringModifiers.lowercaseString;
  if (![typed isEqualToString:@(key.c_str())]) {
    return false;
  }
  // "}" or "+" typed with Shift matches a shortcut written without it.
  return flags == mask ||
         (!IsLetterOrDigit(key) && flags == (mask | NSEventModifierFlagShift));
}

NSMenuItem* _Nullable ReservedMenuItemForEvent(NSEvent* event) {
  if (event.type != NSEventTypeKeyDown || !NSApp.mainMenu) {
    return nil;
  }
  for (const ShortcutAction& action : AllShortcutActions()) {
    for (const Shortcut& shortcut : ShortcutsFor(action.id)) {
      if ((action.reserved || shortcut.key == "tab") &&
          EventMatchesShortcut(event, shortcut)) {
        return FindItem(NSApp.mainMenu, @(std::string(action.id).c_str()));
      }
    }
  }
  return nil;
}

}  // namespace brouk::mac
