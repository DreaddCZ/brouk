// The save prompt of the password manager (008), anchored to the key icon:
// "Save password?" or "Update password?" with the user name and password
// (both editable), and Save, Never for this site and Not now. Main thread
// only.

#import <Cocoa/Cocoa.h>

#include "cef/password_tab.h"

NS_ASSUME_NONNULL_BEGIN

@interface BRPasswordPrompt : NSObject

// Shows the prompt; `done` is called once with the answer (Not now when the
// popover closes without one).
+ (void)showPrompt:(const brouk::PasswordPrompt&)prompt
        relativeTo:(NSView*)anchor
              done:(brouk::PasswordPromptDone)done;

@end

NS_ASSUME_NONNULL_END
