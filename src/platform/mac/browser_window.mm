#import "platform/mac/browser_window.h"

#import "platform/mac/menu_shortcuts.h"

namespace {

// Cmd+Q is reserved in Chrome too, but it is a standard macOS item, not one
// of the remappable browser actions.
bool IsQuit(NSEvent* event) {
  NSEventModifierFlags flags =
      event.modifierFlags &
      (NSEventModifierFlagCommand | NSEventModifierFlagShift |
       NSEventModifierFlagOption | NSEventModifierFlagControl);
  return flags == NSEventModifierFlagCommand &&
         [event.charactersIgnoringModifiers.lowercaseString
             isEqualToString:@"q"];
}

}  // namespace

@implementation BRBrowserWindow

- (void)sendEvent:(NSEvent*)event {
  if (event.type == NSEventTypeKeyDown) {
    NSMenuItem* item = brouk::mac::ReservedMenuItemForEvent(event);
    if (item && [NSApp sendAction:item.action to:item.target from:item]) {
      return;
    }
    if (IsQuit(event) && [NSApp.mainMenu performKeyEquivalent:event]) {
      return;
    }
  }
  [super sendEvent:event];
}

@end
