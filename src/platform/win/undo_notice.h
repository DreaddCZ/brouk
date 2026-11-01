#ifndef BROUK_PLATFORM_WIN_UNDO_NOTICE_H_
#define BROUK_PLATFORM_WIN_UNDO_NOTICE_H_

#include <windows.h>

#include <functional>
#include <string>

#include "platform/win/theme.h"

namespace brouk::win {

// A short notice with one action, such as "Bookmark deleted  Undo" after a
// deletion (011: any deletion can be undone), as the macOS popover. It
// shows for 6 seconds under its anchor without taking the focus; clicking
// the action runs it and closes the notice. UI thread.
class UndoNotice {
 public:
  UndoNotice() = default;
  ~UndoNotice();
  UndoNotice(const UndoNotice&) = delete;
  UndoNotice& operator=(const UndoNotice&) = delete;

  // Shows `message` and `action` under `anchor` (screen pixels), its right
  // edge at the anchor's; a notice already showing is replaced. The font
  // is the window's, which outlives the notice.
  void Show(HWND owner,
            const RECT& anchor,
            std::wstring message,
            std::wstring action,
            std::function<void()> run,
            const Theme& theme,
            HFONT font);
  void Close();
  bool visible() const { return window_ && IsWindowVisible(window_); }

 private:
  static LRESULT CALLBACK WindowProc(HWND window,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
  void Paint();
  int Scale(int dips) const;

  HWND window_ = nullptr;
  HWND owner_ = nullptr;
  std::wstring message_;
  std::wstring action_;
  std::function<void()> run_;
  Theme theme_;
  HFONT font_ = nullptr;
  RECT action_bounds_ = {};  // Client pixels.
  bool action_hovered_ = false;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_UNDO_NOTICE_H_
