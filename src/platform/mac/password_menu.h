// The password drop-down (008) under a login field or the key icon: saved
// logins, a generated password, 2FA codes and "Manage passwords…". Like the
// address suggestions it never takes the keyboard: the page keeps the
// focus, and the tab passes Up, Down, Return and Escape to it first.
// Main thread only.

#import <Cocoa/Cocoa.h>

#include <vector>

#include "cef/password_tab.h"

NS_ASSUME_NONNULL_BEGIN

@interface BRPasswordMenu : NSObject

@property(nonatomic, readonly, getter=isVisible) BOOL visible;

// Shows the items under `rect` (in `view`'s coordinates). `done` gets the
// chosen index, or -1 when the menu closes otherwise; a menu still open is
// closed first.
- (void)showItems:(const std::vector<brouk::PasswordMenuItem>&)items
        belowRect:(NSRect)rect
           inView:(NSView*)view
             done:(brouk::PasswordMenuDone)done;
- (void)close;
// Up (0x26), Down (0x28), Return (0x0D) or Escape (0x1B) while visible;
// returns whether the key was used.
- (BOOL)handleKeyCode:(int)windowsKeyCode;

@end

NS_ASSUME_NONNULL_END
