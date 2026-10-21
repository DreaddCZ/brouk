// Horizontal tab strip drawn in the window's title bar area.
// Main thread only. Renders BRTab state; user actions go to the delegate.

#import <Cocoa/Cocoa.h>

#include "core/tab_strip_model.h"

NS_ASSUME_NONNULL_BEGIN

@class BRTab;

@protocol BRTabStripDelegate <NSObject>
- (void)tabStripDidSelectTab:(brouk::TabId)tabId;
- (void)tabStripDidCloseTab:(brouk::TabId)tabId;
- (void)tabStripDidMoveTab:(brouk::TabId)tabId toIndex:(NSUInteger)index;
- (void)tabStripDidRequestNewTab;
- (void)tabStripDidToggleMute:(brouk::TabId)tabId;
@end

@interface BRTabStripView : NSView

@property(nonatomic, weak) id<BRTabStripDelegate> delegate;
// Space kept free on the left for the window's traffic-light buttons.
@property(nonatomic) CGFloat leadingInset;

// Shows `tabs` in this order with `activeTabId` highlighted.
- (void)setTabs:(NSArray<BRTab*>*)tabs activeTabId:(brouk::TabId)activeTabId;
// Refreshes title, icon and loading state of one tab.
- (void)updateTab:(BRTab*)tab;

@end

NS_ASSUME_NONNULL_END
