// Tab-modal JavaScript dialog (alert, confirm, prompt, "leave site?"): covers
// only its tab's page, so other tabs stay usable, as in Chrome (012).
// Main thread only.

#import <Cocoa/Cocoa.h>

#include "cef/tab_client.h"

NS_ASSUME_NONNULL_BEGIN

@interface BRTabDialogView : NSView

// `done` is called once when the user answers (not when dismissed).
- (instancetype)initWithRequest:(const brouk::JsDialogRequest&)request
                           done:(brouk::JsDialogDone)done;

// Gives keyboard focus to the dialog (prompt field or default button).
- (void)focus;
// Removes the dialog without answering (the page navigated).
- (void)dismiss;

@end

NS_ASSUME_NONNULL_END
