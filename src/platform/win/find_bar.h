#ifndef BROUK_PLATFORM_WIN_FIND_BAR_H_
#define BROUK_PLATFORM_WIN_FIND_BAR_H_

#include <windows.h>

#include <string>

#include "platform/win/theme.h"

namespace brouk::win {

// The find-in-page bar of one tab (012), at the top right of its page as in
// Chrome on Windows: search field, match count, previous, next and close.
// A child window of the tab's container, so each tab keeps its own search.
// UI thread.
class FindBar {
 public:
  class Delegate {
   public:
    // The search text changed (find from the start, or stop if empty).
    virtual void OnFindTextChanged() = 0;
    virtual void OnFindNext(bool forward) = 0;
    // The user closed the bar; the delegate deletes it later, not from
    // inside this call.
    virtual void OnFindClose() = 0;
    // Browser shortcuts typed in the field (Ctrl+T, F3, ...); true if
    // handled.
    virtual bool OnFindShortcut(int key,
                                bool control,
                                bool alt,
                                bool shift) = 0;

   protected:
    virtual ~Delegate() = default;
  };

  FindBar(HWND parent, const Theme& theme, Delegate& delegate);
  ~FindBar();
  FindBar(const FindBar&) = delete;
  FindBar& operator=(const FindBar&) = delete;

  // Places the bar again after the parent's size changed.
  void Place();
  // Focuses the search field and selects its text (Ctrl+F).
  void Focus();
  std::wstring text() const;
  // Shows "3/10", "0/0", or nothing while the field is empty.
  void SetResult(int active, int count);

 private:
  static LRESULT CALLBACK BarProc(HWND window,
                                  UINT message,
                                  WPARAM wparam,
                                  LPARAM lparam);
  static LRESULT CALLBACK FieldProc(HWND window,
                                    UINT message,
                                    WPARAM wparam,
                                    LPARAM lparam,
                                    UINT_PTR id,
                                    DWORD_PTR data);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
  void Layout();
  void Paint();
  void DrawButton(const DRAWITEMSTRUCT& item);
  int Scale(int dips) const;

  HWND parent_;
  Theme theme_;
  Delegate& delegate_;
  UINT dpi_ = 96;

  HWND window_ = nullptr;
  HWND field_ = nullptr;
  HWND count_ = nullptr;
  HWND previous_ = nullptr;
  HWND next_ = nullptr;
  HWND close_ = nullptr;
  HFONT text_font_ = nullptr;
  HFONT glyph_font_ = nullptr;
  HBRUSH background_brush_ = nullptr;
  HBRUSH field_brush_ = nullptr;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_FIND_BAR_H_
