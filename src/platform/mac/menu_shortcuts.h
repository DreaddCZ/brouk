// Keyboard shortcuts from the settings (003) applied to the macOS UI: menu
// key equivalents, Chrome's reserved shortcuts that work even when a page
// handles the key, and tooltips that name the shortcut. Main thread only.

#import <Cocoa/Cocoa.h>

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "core/shortcuts.h"

NS_ASSUME_NONNULL_BEGIN

namespace brouk::mac {

// Posted after the shortcuts changed; views with tooltips refresh them.
extern NSNotificationName const kShortcutsChangedNotification;

// Replaces the shortcuts in effect (brouk::ResolveShortcuts), updates the
// main menu and posts kShortcutsChangedNotification.
void SetShortcuts(
    std::map<std::string, std::vector<Shortcut>, std::less<>> shortcuts);

// Empty if the action has none.
const std::vector<Shortcut>& ShortcutsFor(std::string_view action);

// "Reload (⌘R)", or just "Reload" if the action has no shortcut.
NSString* ToolTip(NSString* text, std::string_view action);

// Sets the key equivalents of every item in `menu` (and its submenus) whose
// identifier is an action id: the first shortcut on the item itself, further
// ones on hidden copies of it.
void ApplyShortcutsToMenu(NSMenu* menu);

bool EventMatchesShortcut(NSEvent* event, const Shortcut& shortcut);

// The menu item to run for `event` before the page sees it: one whose action
// is reserved (Chrome's new tab, close tab, switching tabs…) or whose
// shortcut uses the Tab key (which menus do not handle). Nil otherwise.
NSMenuItem* _Nullable ReservedMenuItemForEvent(NSEvent* event);

}  // namespace brouk::mac

NS_ASSUME_NONNULL_END
