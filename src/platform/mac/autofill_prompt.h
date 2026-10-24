// The autofill prompt (009) after a form was submitted: "Add to <label>?",
// "Update <label>?" with the differences, or "Save as new entry?" with a
// label field. Values failing validation are marked. Main thread only.

#import <Cocoa/Cocoa.h>

#include "cef/autofill_tab.h"

NS_ASSUME_NONNULL_BEGIN

@interface BRAutofillPrompt : NSObject

// `done` is called once (Not now when the popover closes without answer).
+ (void)showPrompt:(const brouk::AutofillPrompt&)prompt
        relativeTo:(NSView*)anchor
              done:(brouk::AutofillPromptDone)done;

@end

NS_ASSUME_NONNULL_END
