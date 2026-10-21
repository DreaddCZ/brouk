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

@end

NS_ASSUME_NONNULL_END
