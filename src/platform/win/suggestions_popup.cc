#include "platform/win/suggestions_popup.h"

#include <windowsx.h>

#include <algorithm>
#include <utility>

#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

constexpr wchar_t kWindowClass[] = L"BroukSuggestions";
// Glyphs of the "Segoe MDL2 Assets" icon font.
constexpr wchar_t kHistoryGlyph[] = L"";
constexpr wchar_t kSearchGlyph[] = L"";
constexpr wchar_t kBookmarkGlyph[] = L"";
constexpr wchar_t kPageGlyph[] = L"";  // Globe.

bool RegisterWindowClass() {
  static const bool registered = [] {
    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_DROPSHADOW;
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = kWindowClass;
    return RegisterClassExW(&window_class) != 0;
  }();
  return registered;
}

const wchar_t* GlyphOf(AddressSuggestion::Kind kind) {
  switch (kind) {
    case AddressSuggestion::Kind::kHistory:
      return kHistoryGlyph;
    case AddressSuggestion::Kind::kBookmark:
      return kBookmarkGlyph;
    case AddressSuggestion::Kind::kTypedUrl:
      return kPageGlyph;
    case AddressSuggestion::Kind::kTypedSearch:
    case AddressSuggestion::Kind::kSearch:
      return kSearchGlyph;
  }
  return kSearchGlyph;
}

}  // namespace

SuggestionsPopup::SuggestionsPopup(HWND owner, Chosen chosen)
    : owner_(owner), chosen_(std::move(chosen)) {}

SuggestionsPopup::~SuggestionsPopup() {
  if (window_) {
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    DestroyWindow(window_);
  }
}

void SuggestionsPopup::Show(std::span<const AddressSuggestion> rows,
                            const RECT& anchor,
                            const Theme& theme,
                            HFONT text_font,
                            HFONT icon_font) {
  if (rows.empty()) {
    Hide();
    return;
  }
  if (!window_) {
    if (!RegisterWindowClass()) {
      return;
    }
    // Owned by the browser window: above it, never activated.
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kWindowClass,
                              L"", WS_POPUP | WS_BORDER, 0, 0, 0, 0, owner_,
                              nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window_) {
      return;
    }
    SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetWindowLongPtrW(
        window_, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(&SuggestionsPopup::WindowProc));
  }
  theme_ = theme;
  text_font_ = text_font;
  icon_font_ = icon_font;
  rows_.clear();
  for (const AddressSuggestion& row : rows) {
    rows_.push_back(
        {GlyphOf(row.kind), Utf8ToWide(row.text), Utf8ToWide(row.detail)});
  }
  selected_ = 0;
  const int height = RowHeight() * count() + 2;  // With the border.
  SetWindowPos(window_, HWND_TOP, anchor.left, anchor.bottom,
               anchor.right - anchor.left, height,
               SWP_NOACTIVATE | SWP_SHOWWINDOW);
  InvalidateRect(window_, nullptr, FALSE);
}

void SuggestionsPopup::Hide() {
  rows_.clear();
  selected_ = -1;
  if (window_) {
    ShowWindow(window_, SW_HIDE);
  }
}

void SuggestionsPopup::Select(int row) {
  selected_ = rows_.empty() ? -1 : std::clamp(row, 0, count() - 1);
  if (window_) {
    InvalidateRect(window_, nullptr, FALSE);
  }
}

int SuggestionsPopup::RowHeight() const {
  return MulDiv(32, static_cast<int>(GetDpiForWindow(owner_)), 96);
}

int SuggestionsPopup::RowAt(int y) const {
  const int row = y / std::max(1, RowHeight());
  return row >= 0 && row < count() ? row : -1;
}

void SuggestionsPopup::Paint() {
  PAINTSTRUCT paint = {};
  HDC dc = BeginPaint(window_, &paint);
  RECT client = {};
  GetClientRect(window_, &client);
  HBRUSH background = CreateSolidBrush(theme_.toolbar);
  FillRect(dc, &client, background);
  DeleteObject(background);
  SetBkMode(dc, TRANSPARENT);
  const int row_height = RowHeight();
  const int dpi = static_cast<int>(GetDpiForWindow(owner_));
  const int margin = MulDiv(12, dpi, 96);
  const int icon_width = MulDiv(28, dpi, 96);
  for (int i = 0; i < count(); ++i) {
    const Row& row = rows_[static_cast<std::size_t>(i)];
    RECT bounds = {client.left, i * row_height, client.right,
                   (i + 1) * row_height};
    if (i == selected_) {
      HBRUSH hover = CreateSolidBrush(theme_.hover);
      FillRect(dc, &bounds, hover);
      DeleteObject(hover);
    }
    SetTextColor(dc, theme_.text_dim);
    SelectObject(dc, icon_font_);
    RECT icon = {bounds.left + margin, bounds.top,
                 bounds.left + margin + icon_width, bounds.bottom};
    DrawTextW(dc, row.glyph.c_str(), -1, &icon,
              DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    // The text, then the detail dimmed after a dash (Chrome).
    SelectObject(dc, text_font_);
    RECT text = {icon.right, bounds.top, bounds.right - margin, bounds.bottom};
    SetTextColor(dc, theme_.text);
    RECT measured = text;
    DrawTextW(dc, row.text.c_str(), -1, &measured,
              DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_CALCRECT);
    const int text_right = std::min(measured.right, text.right);
    RECT shown = {text.left, text.top, text_right, text.bottom};
    DrawTextW(dc, row.text.c_str(), -1, &shown,
              DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    if (!row.detail.empty() && text_right < text.right) {
      SetTextColor(dc, theme_.text_dim);
      const std::wstring detail = L" – " + row.detail;
      RECT rest = {text_right, text.top, text.right, text.bottom};
      DrawTextW(dc, detail.c_str(), -1, &rest,
                DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
  }
  EndPaint(window_, &paint);
}

LRESULT CALLBACK SuggestionsPopup::WindowProc(HWND window,
                                              UINT message,
                                              WPARAM wparam,
                                              LPARAM lparam) {
  auto* self = reinterpret_cast<SuggestionsPopup*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (!self) {
    return DefWindowProcW(window, message, wparam, lparam);
  }
  return self->HandleMessage(message, wparam, lparam);
}

LRESULT SuggestionsPopup::HandleMessage(UINT message,
                                        WPARAM wparam,
                                        LPARAM lparam) {
  switch (message) {
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;  // The address field keeps the keyboard.
    case WM_ERASEBKGND:
      return 1;  // Painted in WM_PAINT.
    case WM_PAINT:
      Paint();
      return 0;
    case WM_MOUSEMOVE:
      if (const int row = RowAt(GET_Y_LPARAM(lparam));
          row >= 0 && row != selected_) {
        Select(row);
      }
      return 0;
    case WM_LBUTTONUP:
      if (const int row = RowAt(GET_Y_LPARAM(lparam)); row >= 0) {
        // May hide or delete the list: nothing here touches it afterwards.
        Chosen chosen = chosen_;
        chosen(row);
      }
      return 0;
    default:
      break;
  }
  return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace brouk::win
