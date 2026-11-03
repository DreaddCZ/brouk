#ifndef BROUK_PLATFORM_WIN_TAB_DRAG_IMAGE_H_
#define BROUK_PLATFORM_WIN_TAB_DRAG_IMAGE_H_

#include <windows.h>

namespace brouk::win {

// The picture of a tab or group label dragged out of the tab strip (026),
// in a small window that follows the mouse, as in Chrome. It takes no
// clicks and never the focus, so the window under it is found and the drag
// goes on. UI thread.
class TabDragImage {
 public:
  // Shows `bitmap` (owned from now on) with its top left corner at
  // `origin` (screen pixels).
  TabDragImage(HBITMAP bitmap, SIZE size, POINT origin);
  ~TabDragImage();
  TabDragImage(const TabDragImage&) = delete;
  TabDragImage& operator=(const TabDragImage&) = delete;

  void MoveTo(POINT origin);
  HWND hwnd() const { return window_; }

 private:
  static LRESULT CALLBACK WindowProc(HWND window,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam);

  HWND window_ = nullptr;
  HBITMAP bitmap_ = nullptr;
  SIZE size_ = {};
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_TAB_DRAG_IMAGE_H_
