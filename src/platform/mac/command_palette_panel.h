// The command palette (003): a search field over a list of actions, tabs and
// settings, shown at the top of a browser window. Ranking comes from
// brouk::RankPaletteItems; this class only shows the list and runs the chosen
// entry. Return runs the selected entry, Escape or clicking elsewhere closes
// it. Main thread only.

#import <Cocoa/Cocoa.h>

#include "core/command_palette.h"

NS_ASSUME_NONNULL_BEGIN

// One thing the palette can run.
@interface BRPaletteEntry : NSObject
@property(nonatomic) brouk::PaletteItem::Kind kind;
@property(nonatomic, copy) NSString* title;
@property(nonatomic, copy) NSString* detail;
@property(nonatomic, nullable) NSImage* image;
// Shown at the right, e.g. "⇧⌘T".
@property(nonatomic, copy, nullable) NSString* shortcut;
// Runs after the palette has closed.
@property(nonatomic, copy) void (^run)(void);
@end

@interface BRCommandPalette : NSObject

// Shows the palette over `window` with an empty query.
- (void)showInWindow:(NSWindow*)window
             entries:(NSArray<BRPaletteEntry*>*)entries;
- (void)close;
@property(nonatomic, readonly, getter=isVisible) BOOL visible;

@end

NS_ASSUME_NONNULL_END
