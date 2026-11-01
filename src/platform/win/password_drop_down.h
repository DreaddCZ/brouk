#ifndef BROUK_PLATFORM_WIN_PASSWORD_DROP_DOWN_H_
#define BROUK_PLATFORM_WIN_PASSWORD_DROP_DOWN_H_

#include <windows.h>

#include <vector>

#include "cef/password_tab.h"
#include "platform/win/theme.h"

namespace brouk::win {

// The password drop-down (008) under a login field or the key icon: saved
// logins, a generated password, 2FA codes and "Manage passwords…"; autofill
// (009) uses it too. Like the address suggestions it never takes the
// keyboard: the page keeps the focus, and the tab passes Up, Down, Enter
// and Escape to it first (HandleKey). A click chooses a row. UI thread.
class PasswordDropDown {
 public:
  // `owner`: the browser window, which the list stays above.
  explicit PasswordDropDown(HWND owner);
  ~PasswordDropDown();
  PasswordDropDown(const PasswordDropDown&) = delete;
  PasswordDropDown& operator=(const PasswordDropDown&) = delete;

  // Shows `items` under `anchor` (screen pixels), at least as wide as it,
  // or above it when there is no room below. A list still open is closed
  // first (its `done` gets -1). `done` gets the chosen index, or -1 when
  // the list closes otherwise. The fonts are the window's; it outlives the
  // list.
  void Show(const std::vector<PasswordMenuItem>& items,
            const RECT& anchor,
            const Theme& theme,
            HFONT text_font,
            HFONT icon_font,
            PasswordMenuDone done);
  void Close();
  bool visible() const { return window_ && IsWindowVisible(window_); }
  // A key typed in the page while the list is shown (Windows key codes).
  // Returns whether the list used it.
  bool HandleKey(int key);

 private:
  static LRESULT CALLBACK WindowProc(HWND window,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
  void Paint();
  int Scale(int dips) const;
  int RowAt(int y) const;
  bool Selectable(int row) const;
  void Select(int row);
  // Moves the selection by `step`, over rows that cannot be chosen.
  void Move(int step);
  // Hides the list and answers `done` with `index` (once).
  void Finish(int index);

  HWND owner_;
  HWND window_ = nullptr;
  std::vector<PasswordMenuItem> items_;
  PasswordMenuDone done_;
  int selected_ = -1;
  Theme theme_;
  HFONT text_font_ = nullptr;
  HFONT icon_font_ = nullptr;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_PASSWORD_DROP_DOWN_H_
