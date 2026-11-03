#import "platform/mac/tab_drag_session.h"

#import "platform/mac/browser_window_controller.h"
#import "platform/mac/tab_strip_view.h"

#include <cmath>

namespace {

constexpr CGFloat kGhostPadding = 6;
constexpr unsigned short kEscapeKeyCode = 53;
// Where the mouse lands in a new window: on its first tab, at the strip's
// middle height.
constexpr CGFloat kNewWindowTabX = 150;
constexpr CGFloat kNewWindowStripMiddle = 19;

// The image of the dragged tab or label on a small panel that follows the
// mouse without taking clicks or the focus.
NSPanel* MakeGhost(NSImage* image) {
  NSSize size = NSMakeSize(image.size.width + 2 * kGhostPadding,
                           image.size.height + 2 * kGhostPadding);
  NSPanel* ghost = [[NSPanel alloc]
      initWithContentRect:NSMakeRect(0, 0, size.width, size.height)
                styleMask:NSWindowStyleMaskBorderless |
                          NSWindowStyleMaskNonactivatingPanel
                  backing:NSBackingStoreBuffered
                    defer:NO];
  ghost.releasedWhenClosed = NO;
  ghost.opaque = NO;
  ghost.backgroundColor = NSColor.clearColor;
  ghost.hasShadow = YES;
  ghost.ignoresMouseEvents = YES;
  ghost.level = NSStatusWindowLevel;
  ghost.alphaValue = 0.9;
  NSView* content =
      [[NSView alloc] initWithFrame:NSMakeRect(0, 0, size.width, size.height)];
  content.wantsLayer = YES;
  content.layer.backgroundColor = NSColor.windowBackgroundColor.CGColor;
  content.layer.cornerRadius = 8;
  NSImageView* view = [NSImageView imageViewWithImage:image];
  view.frame = NSMakeRect(kGhostPadding, kGhostPadding, image.size.width,
                          image.size.height);
  [content addSubview:view];
  ghost.contentView = content;
  return ghost;
}

// Where an event happened on screen. Taken from the event rather than the
// pointer, so events posted to Brouk alone (tests) work too.
NSPoint ScreenPointOf(NSEvent* event) {
  return event.window
             ? [event.window convertPointToScreen:event.locationInWindow]
             : event.locationInWindow;
}

}  // namespace

@implementation BRTabDragSession

+ (void)dragTabs:(const std::vector<brouk::TabId>&)tabIds
         profile:(NSString*)profileId
           group:(BOOL)group
            from:(BRBrowserWindowController*)source
         windows:(NSArray<BRBrowserWindowController*>*)windows
           image:(NSImage*)image
            grab:(NSPoint)grab {
  NSWindow* sourceWindow = source.window;
  // All the window's tabs: the window itself moves (Chrome).
  BOOL moveWindow = tabIds.size() >= source.orderedTabs.count;
  NSPoint start = ScreenPointOf(NSApp.currentEvent);
  NSPoint startOrigin = sourceWindow.frame.origin;
  NSPanel* ghost = nil;
  if (!moveWindow) {
    ghost = MakeGhost(image);
    [ghost setFrameOrigin:NSMakePoint(start.x - grab.x - kGhostPadding,
                                      start.y - grab.y - kGhostPadding)];
    [ghost orderFront:nil];
    [source setFadedTabs:tabIds];
  }
  BRBrowserWindowController* target = nil;
  BRTabDropPlace* place = nil;
  BOOL cancelled = NO;
  NSPoint mouse = start;
  while (true) {
    NSEvent* event =
        [NSApp nextEventMatchingMask:NSEventMaskLeftMouseDragged |
                                     NSEventMaskLeftMouseUp | NSEventMaskKeyDown
                           untilDate:NSDate.distantFuture
                              inMode:NSEventTrackingRunLoopMode
                             dequeue:YES];
    if (event.type == NSEventTypeKeyDown) {
      if (event.keyCode == kEscapeKeyCode) {
        cancelled = YES;
        break;
      }
      continue;
    }
    if (event.type == NSEventTypeLeftMouseUp) {
      break;
    }
    mouse = ScreenPointOf(event);
    if (moveWindow) {
      [sourceWindow
          setFrameOrigin:NSMakePoint(startOrigin.x + mouse.x - start.x,
                                     startOrigin.y + mouse.y - start.y)];
    } else {
      [ghost setFrameOrigin:NSMakePoint(mouse.x - grab.x - kGhostPadding,
                                        mouse.y - grab.y - kGhostPadding)];
    }
    // The front window under the mouse decides; its tab strip takes the
    // tabs, the rest of it does not.
    BRBrowserWindowController* over = nil;
    BRTabDropPlace* overPlace = nil;
    for (BRBrowserWindowController* controller in windows) {
      NSWindow* window = controller.window;
      if ((moveWindow && controller == source) || controller.isPopup ||
          !window.isVisible || window.isMiniaturized ||
          !NSPointInRect(mouse, window.frame)) {
        continue;
      }
      overPlace = [controller
          tabDropPlaceAt:mouse
                 profile:profileId
               excluding:controller == source ? tabIds
                                              : std::vector<brouk::TabId>()
                   group:group];
      if (overPlace) {
        over = controller;
      }
      break;
    }
    if (target && target != over) {
      [target hideTabDropMark];
    }
    target = over;
    place = overPlace;
    // The moving window steps back so the mark under it shows.
    if (moveWindow) {
      sourceWindow.alphaValue = target ? 0.5 : 1;
    }
  }
  [target hideTabDropMark];
  [ghost close];
  [source setFadedTabs:std::vector<brouk::TabId>()];
  sourceWindow.alphaValue = 1;
  if (cancelled) {
    if (moveWindow) {
      [sourceWindow setFrameOrigin:startOrigin];
    }
    return;
  }
  if (!target) {
    // Dropped outside any tab strip: a moved window stays where it is,
    // other tabs open in a new window under the mouse.
    if (!moveWindow) {
      NSRect frame = sourceWindow.frame;
      frame.origin =
          NSMakePoint(mouse.x - kNewWindowTabX,
                      mouse.y - frame.size.height + kNewWindowStripMiddle);
      [source moveTabs:tabIds newWindowFrame:frame];
    }
    return;
  }
  if (target == source) {
    if (group) {
      [source moveGroup:profileId toIndex:place.groupIndex];
    } else if (place.otherProfile) {
      [source askToMoveTab:tabIds.front() toProfile:place.otherProfile];
    } else {
      [source moveTab:tabIds.front() toPosition:place.position];
    }
    return;
  }
  [source moveTabs:tabIds
          toWindow:target
          position:place.position
        groupIndex:place.groupIndex];
  if (!group && place.otherProfile && tabIds.size() == 1) {
    [target askToMoveTab:tabIds.front() toProfile:place.otherProfile];
  }
}

@end
