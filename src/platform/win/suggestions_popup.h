#ifndef BROUK_PLATFORM_WIN_SUGGESTIONS_POPUP_H_
#define BROUK_PLATFORM_WIN_SUGGESTIONS_POPUP_H_

#include <windows.h>

#include <functional>
#include <span>
#include <string>
#include <vector>

#include "core/address_suggestions.h"
#include "platform/win/theme.h"

namespace brouk::win {

// The address bar's suggestion list (003), in a small window under the
// address field, as on macOS. It never takes the keyboard: the field keeps
// it, and the browser window moves the selection (Up/Down) and runs the
// chosen row. Clicking a row chooses it. UI thread.
class SuggestionsPopup {
 public:
  using Chosen = std::function<void(int row)>;

  // `owner`: the browser window, which the list stays above.
  SuggestionsPopup(HWND owner, Chosen chosen);
  ~SuggestionsPopup();
  SuggestionsPopup(const SuggestionsPopup&) = delete;
  SuggestionsPopup& operator=(const SuggestionsPopup&) = delete;

  // Shows `rows` under `anchor` (the address field, in screen pixels), with
  // the first row selected; hides the list if there are none.
  void Show(std::span<const AddressSuggestion> rows,
            const RECT& anchor,
            const Theme& theme,
            HFONT text_font,
            HFONT icon_font);
  void Hide();
  bool visible() const { return window_ && IsWindowVisible(window_); }
  int count() const { return static_cast<int>(rows_.size()); }
  // -1 for none.
  int selected() const { return selected_; }
  void Select(int row);

 private:
  struct Row {
    std::wstring glyph;
    std::wstring text;
    std::wstring detail;
  };

  static LRESULT CALLBACK WindowProc(HWND window,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
  void Paint();
  int RowHeight() const;
  int RowAt(int y) const;

  HWND owner_;
  Chosen chosen_;
  HWND window_ = nullptr;
  std::vector<Row> rows_;
  int selected_ = -1;
  Theme theme_;
  // The browser window's fonts; it outlives the list.
  HFONT text_font_ = nullptr;
  HFONT icon_font_ = nullptr;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_SUGGESTIONS_POPUP_H_
