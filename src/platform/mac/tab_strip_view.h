// Horizontal tab strip drawn in the window's title bar area, with one group
// per profile (003, 005): a label with the profile's name, colour and tab
// count (click collapses or expands the group), the group's tabs, and a "+"
// for a new tab in that profile. When tabs do not fit, it follows
// brouk::LayoutTabStrip: tabs shrink, other groups collapse, and a group
// that is still too big hides some tabs behind an "N more" menu. Main thread
// only. Renders state; user
// actions go to the delegate.

#import <Cocoa/Cocoa.h>

#include "core/tab_strip_model.h"

NS_ASSUME_NONNULL_BEGIN

@class BRTab;

// One profile group as the strip shows it.
@interface BRTabGroupInfo : NSObject
@property(nonatomic, copy) NSString* profileId;
@property(nonatomic, copy) NSString* name;
// One of brouk::ProfileColorNames().
@property(nonatomic, copy) NSString* colorName;
@property(nonatomic) BOOL collapsed;
// Model index of the group's first tab.
@property(nonatomic) NSUInteger startIndex;
// Its first `pinnedCount` tabs are pinned.
@property(nonatomic) NSUInteger pinnedCount;
@property(nonatomic, copy) NSArray<BRTab*>* tabs;
@end

@protocol BRTabStripDelegate <NSObject>
- (void)tabStripDidSelectTab:(brouk::TabId)tabId;
- (void)tabStripDidCloseTab:(brouk::TabId)tabId;
// Reordered inside its group; `index` is the model index.
- (void)tabStripDidMoveTab:(brouk::TabId)tabId toIndex:(NSUInteger)index;
// Dropped on another profile's group: reopen it there (after asking, 005).
- (void)tabStripDidDropTab:(brouk::TabId)tabId onProfile:(NSString*)profileId;
- (void)tabStripDidRequestNewTabInProfile:(NSString*)profileId;
- (void)tabStripDidToggleGroup:(NSString*)profileId;
// The label of a group collapsed for lack of space was clicked: switch to
// the group.
- (void)tabStripDidActivateGroup:(NSString*)profileId;
- (void)tabStripDidSetPinned:(BOOL)pinned tab:(brouk::TabId)tabId;
- (void)tabStripDidReloadTab:(brouk::TabId)tabId;
- (void)tabStripDidRequestNewTabAfter:(brouk::TabId)tabId;
- (void)tabStripDidDuplicateTab:(brouk::TabId)tabId;
// Close Other Tabs / to the Right / to the Left (in the tab's group).
- (void)tabStripDidCloseTabs:(brouk::TabStripModel::CloseScope)scope
                     fromTab:(brouk::TabId)tabId;
- (void)tabStripDidToggleMute:(brouk::TabId)tabId;
// Task manager (020): Pause / Resume Tab and Show in Task Manager.
- (void)tabStripDidTogglePause:(brouk::TabId)tabId;
- (void)tabStripDidShowInTaskManager:(brouk::TabId)tabId;
@end

@interface BRTabStripView : NSView

@property(nonatomic, weak) id<BRTabStripDelegate> delegate;
// Space kept free on the left for the window's traffic-light buttons.
@property(nonatomic) CGFloat leadingInset;

// Shows `groups` left to right with `activeTabId` highlighted.
- (void)setGroups:(NSArray<BRTabGroupInfo*>*)groups
      activeTabId:(brouk::TabId)activeTabId;
// Refreshes title, icon and loading state of one tab.
- (void)updateTab:(BRTab*)tab;

@end

NS_ASSUME_NONNULL_END
