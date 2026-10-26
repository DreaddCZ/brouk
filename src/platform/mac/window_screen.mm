#import "platform/mac/window_screen.h"

#include "core/window_frame.h"

namespace brouk::mac {

namespace {

FrameRect ToFrame(NSRect rect) {
  return {rect.origin.x, rect.origin.y, rect.size.width, rect.size.height};
}

}  // namespace

void FitWindowToItsScreen(NSWindow* window) {
  NSScreen* screen = window.screen;
  if (!screen || NSEvent.pressedMouseButtons != 0 ||
      (window.styleMask & NSWindowStyleMaskFullScreen) != 0) {
    return;
  }
  FrameRect frame = ToFrame(window.frame);
  FrameRect fitted =
      FitFrameToArea(frame, ToFrame(screen.visibleFrame), window.minSize.width,
                     window.minSize.height);
  if (fitted != frame) {
    [window setFrame:NSMakeRect(fitted.x, fitted.y, fitted.width, fitted.height)
             display:YES];
  }
}

void NotifyScreenChanged(const CefRefPtr<CefBrowser>& browser) {
  if (browser) {
    browser->GetHost()->NotifyScreenInfoChanged();
  }
}

}  // namespace brouk::mac
