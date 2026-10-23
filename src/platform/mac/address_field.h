// The address bar text field. Shows the shortened URL until clicked, then the
// full URL with everything selected, as Chrome does. Main thread only.

#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

@class BRAddressField;

@protocol BRAddressFieldDelegate <NSObject>
// The user pressed Return.
- (void)addressField:(BRAddressField*)field didSubmit:(NSString*)text;
// The user pressed Escape: show the current page's URL again.
- (void)addressFieldDidCancel:(BRAddressField*)field;
// Typing changed the text (kept per tab while switching tabs).
- (void)addressFieldDidEdit:(BRAddressField*)field;
@optional
// Keys for the suggestion list (003), before the field handles them:
// moveUp:, moveDown:, cancelOperation: and, for ⇧⌫, deleteBackward: with
// Shift. Returns YES if handled.
- (BOOL)addressField:(BRAddressField*)field doCommand:(SEL)command;
// Editing ended (the suggestion list closes).
- (void)addressFieldDidEndEditing:(BRAddressField*)field;
@end

@interface BRAddressField : NSTextField

@property(nonatomic, weak) id<BRAddressFieldDelegate> addressDelegate;
// Full URL of the page; shown shortened while the field is not focused.
@property(nonatomic, copy) NSString* pageURL;
@property(nonatomic, readonly) BOOL isEditing;

// Focuses the field and selects its text (Cmd+L).
- (void)focusAndSelectAll;
// Shows `text` as unsubmitted input (restored when switching back to a tab).
- (void)showPendingText:(nullable NSString*)text;

// What the user typed, without an inline completion or a suggestion shown
// while moving through the list.
@property(nonatomic, readonly, copy) NSString* typedText;
// Completes the typed text inline with `completion`, selected so that typing
// replaces it (as Chrome does). Only if the text is still `typed` and the
// last edit added text at the end (not after deleting, not while composing
// accented characters).
- (void)setInlineCompletion:(NSString*)completion forTypedText:(NSString*)typed;
// Shows a suggestion's text while moving through the list (Up/Down);
// `text` nil shows the typed text again.
- (void)showSuggestionText:(nullable NSString*)text;

@end

NS_ASSUME_NONNULL_END
