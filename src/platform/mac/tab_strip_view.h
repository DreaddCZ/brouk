// Horizontal tab strip drawn in the window's title bar area, with one group
// per profile (003, 005): a label with the profile's name, colour and tab
// count (click collapses or expands the group), the group's tabs, and a "+"
// for a new tab in that profile. When tabs do not fit, it follows
// brouk::LayoutTabStrip: tabs shrink, other groups collapse, and a group
// that is still too big hides some tabs behind an "N more" menu. Main thread
// only. Renders state; user
// actions go to the delegate.

#import <Cocoa/Cocoa.h>

#include <cstdint>
#include <vector>

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

// Where tabs dropped on the strip go (026).
@interface BRTabDropPlace : NSObject
// Position inside the dragged tabs' profile group (tabs being dragged in
// this strip not counted).
@property(nonatomic) NSUInteger position;
// The group's place among the groups if it has to be created, or moved
// (a dragged group), not counting the dragged group itself.
@property(nonatomic) NSUInteger groupIndex;
// Dropped inside another profile's group: that profile, else nil.
@property(nonatomic, copy, nullable) NSString* otherProfile;
// Where the insertion mark goes (strip coordinates).
@property(nonatomic) CGFloat markX;
@end

// A window another window's tab or group can be moved to (menus, 026).
@interface BRWindowChoice : NSObject
@property(nonatomic) std::int64_t windowId;
@property(nonatomic, copy) NSString* title;
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
// Moving tabs and groups (026). A group's label was dragged to another
// place in the strip; `index` counts the groups without it.
- (void)tabStripDidMoveGroup:(NSString*)profileId toIndex:(NSUInteger)index;
// A tab, or a group's label (`group`; `tabIds` are then all its tabs), was
// dragged out of the strip: the delegate carries on with the drag until
// the mouse button goes up. `image` follows the mouse, held at `grab`.
- (void)tabStripDidDragOutTabs:(const std::vector<brouk::TabId>&)tabIds
                       profile:(NSString*)profileId
                         group:(BOOL)group
                         image:(NSImage*)image
                          grab:(NSPoint)grab;
// The other browser windows, and "Move … to Another Window": windowId 0 is
// a new window.
- (NSArray<BRWindowChoice*>*)tabStripOtherWindows;
- (void)tabStripDidMoveTabs:(const std::vector<brouk::TabId>&)tabIds
                   toWindow:(std::int64_t)windowId;
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

// Dropping tabs (026): where tabs of `profileId` dropped at `point` (strip
// coordinates) would go, not counting `excluded` tabs (those dragged from
// this strip) and, for a dragged group, its own group.
- (BRTabDropPlace*)dropPlaceForProfile:(NSString*)profileId
                               atPoint:(NSPoint)point
                             excluding:
                                 (const std::vector<brouk::TabId>&)excluded
                                 group:(BOOL)group;
// The insertion mark while tabs are dragged over the strip; NAN hides it.
- (void)showDropMarkAtX:(CGFloat)x;
// Tabs being dragged out are shown faded.
- (void)setFadedTabs:(const std::vector<brouk::TabId>&)tabIds;

@end

NS_ASSUME_NONNULL_END
