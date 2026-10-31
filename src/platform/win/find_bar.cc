#include "platform/win/find_bar.h"

#include <commctrl.h>

#include <algorithm>
#include <string>

#include "platform/win/controls.h"

namespace brouk::win {

namespace {

constexpr wchar_t kBarClass[] = L"BroukFindBar";

// Glyphs of the "Segoe MDL2 Assets" icon font.
constexpr wchar_t kPreviousGlyph[] = L"\uE70E";
constexpr wchar_t kNextGlyph[] = L"\uE70D";
constexpr wchar_t kCloseGlyph[] = L"\uE711";

// Layout in device-independent pixels (96 DPI).
constexpr int kWidth = 380;
constexpr int kHeight = 48;
constexpr int kRightMargin = 16;
constexpr int kPadding = 8;
constexpr int kButtonSize = 32;
constexpr int kCountWidth = 56;
constexpr int kFieldHeight = 32;

enum ControlId : int {
  kFieldId = 1,
  kCountId,
  kPreviousId,
  kNextId,
  kCloseId,
};

bool RegisterBarClass(WNDPROC procedure) {
  static const bool registered = [procedure] {
    WNDCLASSEXW bar_class = {};
    bar_class.cbSize = sizeof(bar_class);
    bar_class.lpfnWndProc = procedure;
    bar_class.hInstance = GetModuleHandleW(nullptr);
    bar_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    bar_class.lpszClassName = kBarClass;
    return RegisterClassExW(&bar_class) != 0;
  }();
  return registered;
}

}  // namespace

FindBar::FindBar(HWND parent, const Theme& theme, Delegate& delegate)
    : parent_(parent),
      theme_(theme),
      delegate_(delegate),
      dpi_(GetDpiForWindow(parent)) {
  background_brush_ = CreateSolidBrush(theme_.toolbar);
  field_brush_ = CreateSolidBrush(theme_.field);
  NONCLIENTMETRICSW metrics = {};
  metrics.cbSize = sizeof(metrics);
  SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics,
                             0, dpi_);
  text_font_ = CreateFontIndirectW(&metrics.lfMessageFont);
  LOGFONTW glyph = {};
  glyph.lfHeight = -Scale(12);
  glyph.lfWeight = FW_NORMAL;
  glyph.lfCharSet = DEFAULT_CHARSET;
  glyph.lfQuality = CLEARTYPE_QUALITY;
  wcscpy_s(glyph.lfFaceName, L"Segoe MDL2 Assets");
  glyph_font_ = CreateFontIndirectW(&glyph);
  if (!RegisterBarClass(&FindBar::BarProc)) {
    return;
  }
  window_ =
      CreateWindowExW(0, kBarClass, L"Find in page",
                      WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0,
                      parent_, nullptr, GetModuleHandleW(nullptr), this);
  if (!window_) {
    return;
  }
  HINSTANCE instance = GetModuleHandleW(nullptr);
  auto add = [&](const wchar_t* control_class, const wchar_t* text, DWORD style,
                 int id, HFONT font) {
    HWND control = CreateWindowExW(
        0, control_class, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
        window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance,
        nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    return control;
  };
  field_ =
      add(WC_EDITW, L"", ES_AUTOHSCROLL | WS_TABSTOP, kFieldId, text_font_);
  SendMessageW(field_, EM_SETCUEBANNER, TRUE,
               reinterpret_cast<LPARAM>(L"Find in page"));
  SetAccessibleName(field_, L"Find in page");
  SetWindowSubclass(field_, &FindBar::FieldProc, 0,
                    reinterpret_cast<DWORD_PTR>(this));
  count_ = add(WC_STATICW, L"", SS_RIGHT | SS_NOPREFIX | SS_CENTERIMAGE,
               kCountId, text_font_);
  previous_ =
      add(WC_BUTTONW, kPreviousGlyph, BS_OWNERDRAW, kPreviousId, glyph_font_);
  SetAccessibleName(previous_, L"Previous match");
  next_ = add(WC_BUTTONW, kNextGlyph, BS_OWNERDRAW, kNextId, glyph_font_);
  SetAccessibleName(next_, L"Next match");
  close_ = add(WC_BUTTONW, kCloseGlyph, BS_OWNERDRAW, kCloseId, glyph_font_);
  SetAccessibleName(close_, L"Close find bar");
  Layout();
  const int width = Scale(kWidth);
  const int height = Scale(kHeight);
  const int radius = Scale(8);
  // The system owns the region from here on.
  SetWindowRgn(window_,
               CreateRoundRectRgn(0, 0, width + 1, height + 1, radius, radius),
               FALSE);
  Place();
  ShowWindow(window_, SW_SHOWNA);
}

FindBar::~FindBar() {
  if (window_) {
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    DestroyWindow(window_);
  }
  for (HFONT font : {text_font_, glyph_font_}) {
    if (font) {
      DeleteObject(font);
    }
  }
  for (HBRUSH brush : {background_brush_, field_brush_}) {
    if (brush) {
      DeleteObject(brush);
    }
  }
}

int FindBar::Scale(int dips) const {
  return MulDiv(dips, static_cast<int>(dpi_), 96);
}

void FindBar::Layout() {
  const int height = Scale(kHeight);
  const int button = Scale(kButtonSize);
  const int top = (height - button) / 2;
  int right = Scale(kWidth) - Scale(kPadding);
  for (HWND control : {close_, next_, previous_}) {
    right -= button;
    SetWindowPos(control, nullptr, right, top, button, button,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }
  right -= Scale(kPadding);
  const int count_width = Scale(kCountWidth);
  right -= count_width;
  SetWindowPos(count_, nullptr, right, top, count_width, button,
               SWP_NOZORDER | SWP_NOACTIVATE);
  // The edit sits inside a rounded field painted by the bar.
  HDC dc = GetDC(window_);
  HGDIOBJ previous = SelectObject(dc, text_font_);
  TEXTMETRICW metrics = {};
  GetTextMetricsW(dc, &metrics);
  SelectObject(dc, previous);
  ReleaseDC(window_, dc);
  const int left = Scale(kPadding) + Scale(10);
  const int edit_height = metrics.tmHeight + Scale(2);
  SetWindowPos(field_, nullptr, left, (height - edit_height) / 2,
               std::max(0, right - Scale(kPadding) - left), edit_height,
               SWP_NOZORDER | SWP_NOACTIVATE);
}

void FindBar::Place() {
  if (!window_) {
    return;
  }
  RECT parent = {};
  GetClientRect(parent_, &parent);
  const int width = Scale(kWidth);
  SetWindowPos(
      window_, HWND_TOP,
      std::max(0, static_cast<int>(parent.right) - width - Scale(kRightMargin)),
      0, width, Scale(kHeight), SWP_NOACTIVATE);
}

void FindBar::Focus() {
  SetFocus(field_);
  SendMessageW(field_, EM_SETSEL, 0, -1);
}

bool FindBar::HasFocus() const {
  return field_ && GetFocus() == field_;
}

void FindBar::Refocus() {
  SetFocus(field_);
}

std::wstring FindBar::text() const {
  return WindowText(field_);
}

void FindBar::SetResult(int active, int count) {
  std::wstring text;
  if (GetWindowTextLengthW(field_) > 0) {
    text =
        std::to_wstring(count > 0 ? active : 0) + L"/" + std::to_wstring(count);
  }
  SetWindowTextW(count_, text.c_str());
}

void FindBar::Paint() {
  PAINTSTRUCT paint = {};
  HDC dc = BeginPaint(window_, &paint);
  RECT client = {};
  GetClientRect(window_, &client);
  FillRect(dc, &client, background_brush_);
  HGDIOBJ pen = SelectObject(dc, GetStockObject(DC_PEN));
  HGDIOBJ brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
  SetDCPenColor(dc, theme_.separator);
  const int radius = Scale(8);
  RoundRect(dc, 0, 0, client.right, client.bottom, radius, radius);
  // The search field's rounded background.
  RECT count = {};
  GetWindowRect(count_, &count);
  MapWindowPoints(nullptr, window_, reinterpret_cast<POINT*>(&count), 2);
  const int field_height = Scale(kFieldHeight);
  const int top = (static_cast<int>(client.bottom) - field_height) / 2;
  SelectObject(dc, GetStockObject(DC_BRUSH));
  SetDCBrushColor(dc, theme_.field);
  SetDCPenColor(dc, theme_.field);
  RoundRect(dc, Scale(kPadding), top, count.right, top + field_height,
            field_height, field_height);
  SelectObject(dc, brush);
  SelectObject(dc, pen);
  EndPaint(window_, &paint);
}

void FindBar::DrawButton(const DRAWITEMSTRUCT& item) {
  const bool pressed = (item.itemState & ODS_SELECTED) != 0;
  SetDCBrushColor(item.hDC, pressed ? theme_.hover : theme_.toolbar);
  FillRect(item.hDC, &item.rcItem,
           static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
  wchar_t glyph[4] = {};
  GetWindowTextW(item.hwndItem, glyph, 4);
  HGDIOBJ previous = SelectObject(item.hDC, glyph_font_);
  SetBkMode(item.hDC, TRANSPARENT);
  SetTextColor(item.hDC, theme_.text);
  RECT area = item.rcItem;
  DrawTextW(item.hDC, glyph, -1, &area,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  SelectObject(item.hDC, previous);
}

// static
LRESULT CALLBACK FindBar::BarProc(HWND window,
                                  UINT message,
                                  WPARAM wparam,
                                  LPARAM lparam) {
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(window, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  }
  auto* self =
      reinterpret_cast<FindBar*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (!self || !self->window_) {
    return DefWindowProcW(window, message, wparam, lparam);
  }
  return self->HandleMessage(message, wparam, lparam);
}

LRESULT FindBar::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_PAINT:
      Paint();
      return 0;
    case WM_ERASEBKGND:
      return 1;  // Painted in WM_PAINT.
    case WM_DRAWITEM:
      DrawButton(*reinterpret_cast<const DRAWITEMSTRUCT*>(lparam));
      return TRUE;
    case WM_CTLCOLOREDIT: {
      HDC dc = reinterpret_cast<HDC>(wparam);
      SetTextColor(dc, theme_.text);
      SetBkColor(dc, theme_.field);
      return reinterpret_cast<LRESULT>(field_brush_);
    }
    case WM_CTLCOLORSTATIC: {  // The match count.
      HDC dc = reinterpret_cast<HDC>(wparam);
      SetTextColor(dc, theme_.text_dim);
      SetBkColor(dc, theme_.field);
      return reinterpret_cast<LRESULT>(field_brush_);
    }
    case WM_COMMAND:
      switch (LOWORD(wparam)) {
        case kFieldId:
          if (HIWORD(wparam) == EN_CHANGE) {
            delegate_.OnFindTextChanged();
          }
          return 0;
        case kPreviousId:
          delegate_.OnFindNext(false);
          return 0;
        case kNextId:
          delegate_.OnFindNext(true);
          return 0;
        case kCloseId:
          delegate_.OnFindClose();
          return 0;
        default:
          break;
      }
      break;
    default:
      break;
  }
  return DefWindowProcW(window_, message, wparam, lparam);
}

// static
LRESULT CALLBACK FindBar::FieldProc(HWND window,
                                    UINT message,
                                    WPARAM wparam,
                                    LPARAM lparam,
                                    UINT_PTR /*id*/,
                                    DWORD_PTR data) {
  auto* self = reinterpret_cast<FindBar*>(data);
  switch (message) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
      const int key = static_cast<int>(wparam);
      const bool control = KeyDown(VK_CONTROL);
      const bool alt = KeyDown(VK_MENU);
      const bool shift = KeyDown(VK_SHIFT);
      // Enter: next match, Shift+Enter: previous, Escape: close (Chrome).
      if (key == VK_RETURN && !control && !alt) {
        self->delegate_.OnFindNext(!shift);
        return 0;
      }
      if (key == VK_ESCAPE) {
        self->delegate_.OnFindClose();
        return 0;
      }
      if (control || alt || (key >= VK_F1 && key <= VK_F24)) {
        if (self->delegate_.OnFindShortcut(key, control, alt, shift)) {
          return 0;
        }
      }
      break;
    }
    case WM_CHAR:
      // Enter, Escape and Tab: without this the edit beeps.
      if (wparam == L'\r' || wparam == 0x1B || wparam == L'\t') {
        return 0;
      }
      break;
    case WM_NCDESTROY:
      RemoveWindowSubclass(window, &FindBar::FieldProc, 0);
      break;
    default:
      break;
  }
  return DefSubclassProc(window, message, wparam, lparam);
}

}  // namespace brouk::win
