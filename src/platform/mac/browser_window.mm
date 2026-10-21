#import "platform/mac/browser_window.h"

#import <Carbon/Carbon.h>  // kVK_* key codes.

namespace {

constexpr NSEventModifierFlags kRelevantModifiers =
    NSEventModifierFlagCommand | NSEventModifierFlagShift |
    NSEventModifierFlagOption | NSEventModifierFlagControl;

// Returns the action for Ctrl+Tab / Ctrl+Shift+Tab, which have no menu item.
SEL ControlTabAction(NSEvent* event, NSEventModifierFlags flags) {
  if (event.keyCode != kVK_Tab) {
    return nullptr;
  }
  if (flags == NSEventModifierFlagControl) {
    return @selector(selectNextTab:);
  }
  if (flags == (NSEventModifierFlagControl | NSEventModifierFlagShift)) {
    return @selector(selectPreviousTab:);
  }
  return nullptr;
}

// Chrome's reserved browser shortcuts that are menu items. Letters are
// matched by character (keyboard layouts move them), arrows by key code.
bool IsReservedMenuShortcut(NSEvent* event, NSEventModifierFlags flags) {
  const NSEventModifierFlags cmd = NSEventModifierFlagCommand;
  const NSEventModifierFlags cmd_shift = cmd | NSEventModifierFlagShift;
  const NSEventModifierFlags cmd_opt = cmd | NSEventModifierFlagOption;
  if (event.keyCode == kVK_LeftArrow || event.keyCode == kVK_RightArrow) {
    return flags == cmd_opt;
  }
  NSString* key = event.charactersIgnoringModifiers.lowercaseString;
  if ([key isEqualToString:@"t"] || [key isEqualToString:@"n"] ||
      [key isEqualToString:@"q"]) {
    return flags == cmd;
  }
  if ([key isEqualToString:@"w"]) {
    return flags == cmd || flags == cmd_shift;
  }
  return false;
}

}  // namespace

@implementation BRBrowserWindow

- (void)sendEvent:(NSEvent*)event {
  if (event.type == NSEventTypeKeyDown) {
    NSEventModifierFlags flags = event.modifierFlags & kRelevantModifiers;
    SEL action = ControlTabAction(event, flags);
    if (action && [NSApp sendAction:action to:nil from:self]) {
      return;
    }
    if (IsReservedMenuShortcut(event, flags) &&
        [NSApp.mainMenu performKeyEquivalent:event]) {
      return;
    }
  }
  [super sendEvent:event];
}

@end
