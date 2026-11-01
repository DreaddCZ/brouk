#ifndef BROUK_PLATFORM_WIN_DEVTOOLS_FRAME_H_
#define BROUK_PLATFORM_WIN_DEVTOOLS_FRAME_H_

#include <windows.h>

#include <functional>
#include <string>

namespace brouk::win {

// A window of its own for developer tools (006): a tab's dock moved out of
// the tab ("Open in a separate window"), or DevTools for one of Brouk's
// pages. The dock's window goes in it and fills it. Closing the frame asks
// for the dock to close (`close_requested`); the owner then deletes the
// frame. UI thread.
class DevToolsFrame {
 public:
  DevToolsFrame(HWND beside, std::function<void()> close_requested);
  ~DevToolsFrame();
  DevToolsFrame(const DevToolsFrame&) = delete;
  DevToolsFrame& operator=(const DevToolsFrame&) = delete;

  HWND hwnd() const { return window_; }
  void SetTitle(const std::wstring& title);
  // Puts `child` in the frame and shows it in front.
  void Show(HWND child);

 private:
  static LRESULT CALLBACK WindowProc(HWND window,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam);

  HWND window_ = nullptr;
  HWND child_ = nullptr;
  std::function<void()> close_requested_;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_DEVTOOLS_FRAME_H_
