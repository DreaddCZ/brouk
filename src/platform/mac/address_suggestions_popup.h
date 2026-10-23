// The address bar's suggestion list (003), in a small window under the
// address field. It never takes the keyboard: the field keeps it, and the
// window controller moves the selection (Up/Down) and runs the chosen row.
// Main thread only.

#import <Cocoa/Cocoa.h>

#include <span>

#include "core/address_suggestions.h"

NS_ASSUME_NONNULL_BEGIN

@class BRSuggestionsPopup;

@protocol BRSuggestionsPopupDelegate <NSObject>
// A row was clicked.
- (void)suggestionsPopup:(BRSuggestionsPopup*)popup didChooseRow:(NSInteger)row;
@end

@interface BRSuggestionsPopup : NSObject

@property(nonatomic, weak) id<BRSuggestionsPopupDelegate> delegate;
@property(nonatomic, readonly, getter=isVisible) BOOL visible;
// -1 for none.
@property(nonatomic) NSInteger selectedRow;
@property(nonatomic, readonly) NSInteger rowCount;

// Shows `rows` under `field` (hides the list if there are none), with the
// first row selected.
- (void)showRows:(std::span<const brouk::AddressSuggestion>)rows
       belowView:(NSView*)field;
- (void)close;

@end

NS_ASSUME_NONNULL_END
