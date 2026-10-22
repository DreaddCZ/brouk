// Tab-modal dialog: covers only its tab's page, so other tabs stay usable,
// as in Chrome (012). Used for JavaScript dialogs (alert, confirm, prompt,
// "leave site?") and for "page unresponsive". Main thread only.

#import <Cocoa/Cocoa.h>

#include "cef/tab_client.h"

NS_ASSUME_NONNULL_BEGIN

// Called once when the user answers (not when dismissed). `input` is the
// prompt text (empty without a prompt field).
typedef void (^BRTabDialogCompletion)(BOOL ok, NSString* input, BOOL suppress);

@interface BRTabDialogView : NSView

// `promptText` non-nil adds a text field; `checkboxTitle` non-nil adds a
// checkbox (its state is `suppress` in the completion); `cancelTitle` nil
// means no Cancel button (Escape then answers OK, like an alert).
- (instancetype)initWithTitle:(NSString*)title
                      message:(NSString*)message
                      okTitle:(NSString*)okTitle
                  cancelTitle:(nullable NSString*)cancelTitle
                   promptText:(nullable NSString*)promptText
                checkboxTitle:(nullable NSString*)checkboxTitle
                   completion:(BRTabDialogCompletion)completion;

// A JavaScript dialog; `done` is called once when the user answers.
+ (instancetype)dialogForRequest:(const brouk::JsDialogRequest&)request
                            done:(brouk::JsDialogDone)done;

// Gives keyboard focus to the dialog (prompt field or default button).
- (void)focus;
// Removes the dialog without answering (the page navigated).
- (void)dismiss;

@end

NS_ASSUME_NONNULL_END
