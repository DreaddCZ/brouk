// Browser window. Handles Chrome's "reserved" shortcuts (new/close tab or
// window, quit, switch tabs; remappable, see menu_shortcuts.h) before web
// content sees them, so a page cannot block them. Main thread only.

#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

@interface BRBrowserWindow : NSWindow
@end

NS_ASSUME_NONNULL_END
