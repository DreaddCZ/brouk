// Keeping windows usable when they change display (another Space on another
// display, a display disconnected). Main thread only.

#import <Cocoa/Cocoa.h>

#include "include/cef_browser.h"

NS_ASSUME_NONNULL_BEGIN

namespace brouk::mac {

// Fits `window` to the usable part of its display if it does not fit (see
// FitFrameToArea). Not while the user drags it: they place it themselves.
void FitWindowToItsScreen(NSWindow* window);

// Tells a browser its display changed (size, scale factor), so the page is
// drawn for it; otherwise it can stay black or keep the old size.
void NotifyScreenChanged(const CefRefPtr<CefBrowser>& browser);

}  // namespace brouk::mac

NS_ASSUME_NONNULL_END
