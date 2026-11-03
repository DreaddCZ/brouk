// A tab or a profile group dragged out of a window's tab strip (026), as in
// Chrome: an image of it follows the mouse; dropped on a browser window's
// tab strip it joins that window at the marked place, dropped anywhere else
// it opens in a new window there. When it holds all of its window's tabs,
// the window itself follows the mouse instead. Escape cancels. Main thread
// only.

#import <Cocoa/Cocoa.h>

#include <vector>

#include "core/tab_strip_model.h"

NS_ASSUME_NONNULL_BEGIN

@class BRBrowserWindowController;

@interface BRTabDragSession : NSObject

// Runs until the mouse button goes up. `windows`: the browser windows, front
// first; `group`: the group's label was dragged (`tabIds` are its tabs).
+ (void)dragTabs:(const std::vector<brouk::TabId>&)tabIds
         profile:(NSString*)profileId
           group:(BOOL)group
            from:(BRBrowserWindowController*)source
         windows:(NSArray<BRBrowserWindowController*>*)windows
           image:(NSImage*)image
            grab:(NSPoint)grab;

@end

NS_ASSUME_NONNULL_END
