// Find-in-page bar shown below the toolbar (012): search field, match count,
// previous/next and close. Main thread only.

#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

@class BRFindBar;

@protocol BRFindBarDelegate <NSObject>
// The text changed or the user asked for the next/previous match.
- (void)findBar:(BRFindBar*)bar
       findText:(NSString*)text
        forward:(BOOL)forward
       findNext:(BOOL)findNext;
- (void)findBarDidClose:(BRFindBar*)bar;
@end

@interface BRFindBar : NSView

@property(nonatomic, weak) id<BRFindBarDelegate> delegate;
@property(nonatomic, copy) NSString* text;

// Focuses the search field and selects its text (Cmd+F).
- (void)focus;
// Shows "3/10", "0/0" or nothing (empty text).
- (void)setActiveMatch:(int)active ofCount:(int)count;

@end

NS_ASSUME_NONNULL_END
