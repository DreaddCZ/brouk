#include "platform/win/password_drop_down.h"

#include <windowsx.h>

#include <algorithm>
#include <string>
#include <utility>

#include "platform/win/controls.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

constexpr wchar_t kWindowClass[] = L"BroukPasswordList";
constexpr int kRowHeight = 40;
constexpr int kPadding = 4;
constexpr int kMinWidth = 260;
constexpr int kGap = 2;

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

// Glyphs of the "Segoe MDL2 Assets" icon font, close to the macOS symbols.
const wchar_t* GlyphOf(PasswordMenuItem::Kind kind) {
  using Kind = PasswordMenuItem::Kind;
  switch (kind) {
    case Kind::kLogin:
      return L"";  // Permissions (a key).
    case Kind::kGeneratedPassword:
      return L"";  // LightningBolt.
    case Kind::kCode:
      return L"";  // Stopwatch.
    case Kind::kReadQrCode:
      return L"";  // QRCode.
    case Kind::kAutofillEntry:
      return L"";  // ContactInfo.
    case Kind::kCustomField:
      return L"";  // Add.
    case Kind::kClearFilled:
      return L"";  // EraseTool.
    case Kind::kManage:
    case Kind::kManageAutofill:
      return L"";  // Setting.
    case Kind::kWarning:
      return L"";  // Warning.
    case Kind::kUnlock:
      return L"";  // Lock.
  }
  return L"";
}

// Orange, as the macOS list's warnings, readable on either background.
COLORREF WarningColor(const Theme& theme) {
  return theme.dark ? RGB(0xFF, 0xB9, 0x4D) : RGB(0xB3, 0x5C, 0x00);
}

std::wstring DetailOf(const PasswordMenuItem& item) {
  std::wstring detail = Utf8ToWide(item.detail);
  if (item.kind == PasswordMenuItem::Kind::kCode) {
    detail += L"  ·  " + std::to_wstring(item.seconds_left) + L" s left";
  }
  return detail;
}

}  // namespace

PasswordDropDown::PasswordDropDown(HWND owner) : owner_(owner) {}

PasswordDropDown::~PasswordDropDown() {
  done_ = nullptr;  // No answers from a list being torn down.
  if (window_) {
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    DestroyWindow(window_);
  }
}

int PasswordDropDown::Scale(int dips) const {
  return MulDiv(dips, static_cast<int>(GetDpiForWindow(owner_)), 96);
}

void PasswordDropDown::Show(const std::vector<PasswordMenuItem>& items,
                            const RECT& anchor,
                            const Theme& theme,
                            HFONT text_font,
                            HFONT icon_font,
                            PasswordMenuDone done) {
  Close();
  if (items.empty() || !RegisterWindowClass()) {
    if (done) {
      done(-1);
    }
    return;
  }
  if (!window_) {
    // Owned by the browser window: above it, never activated.
    window_ =
        CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kWindowClass,
                        L"Passwords", WS_POPUP | WS_BORDER, 0, 0, 0, 0, owner_,
                        nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window_) {
      if (done) {
        done(-1);
      }
      return;
    }
    SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetWindowLongPtrW(
        window_, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(&PasswordDropDown::WindowProc));
  }
  items_ = items;
  done_ = std::move(done);
  theme_ = theme;
  text_font_ = text_font;
  icon_font_ = icon_font;
  selected_ = -1;
  const int width =
      std::max(static_cast<int>(anchor.right - anchor.left), Scale(kMinWidth));
  const int height = Scale(kRowHeight) * static_cast<int>(items_.size()) +
                     2 * Scale(kPadding) + 2;  // With the border.
  int top = anchor.bottom + Scale(kGap);
  // Above the field when there is no room below it.
  MONITORINFO monitor = {};
  monitor.cbSize = sizeof(monitor);
  if (GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST),
                      &monitor) &&
      top + height > monitor.rcWork.bottom) {
    top = anchor.top - Scale(kGap) - height;
  }
  SetWindowPos(window_, HWND_TOP, anchor.left, top, width, height,
               SWP_NOACTIVATE | SWP_SHOWWINDOW);
  // The rows are announced by name (the list has no UI Automation rows
  // yet, see 024); the first item names the list.
  SetAccessibleName(window_, Utf8ToWide(items_.front().title).c_str());
  InvalidateRect(window_, nullptr, FALSE);
}

void PasswordDropDown::Close() {
  Finish(-1);
}

void PasswordDropDown::Finish(int index) {
  if (window_) {
    ShowWindow(window_, SW_HIDE);
  }
  items_.clear();
  selected_ = -1;
  // May show another list or delete this one: nothing here touches it after.
  PasswordMenuDone done = std::move(done_);
  done_ = nullptr;
  if (done) {
    done(index);
  }
}

bool PasswordDropDown::Selectable(int row) const {
  return row >= 0 && row < static_cast<int>(items_.size()) &&
         items_[static_cast<std::size_t>(row)].kind !=
             PasswordMenuItem::Kind::kWarning;
}

void PasswordDropDown::Select(int row) {
  if (row != selected_) {
    selected_ = row;
    InvalidateRect(window_, nullptr, FALSE);
  }
}

void PasswordDropDown::Move(int step) {
  const int count = static_cast<int>(items_.size());
  int index = selected_;
  for (int tries = 0; tries < count; ++tries) {
    index =
        index < 0 ? (step > 0 ? 0 : count - 1) : (index + step + count) % count;
    if (Selectable(index)) {
      Select(index);
      return;
    }
  }
}

bool PasswordDropDown::HandleKey(int key) {
  if (!visible()) {
    return false;
  }
  switch (key) {
    case VK_UP:
      Move(-1);
      return true;
    case VK_DOWN:
      Move(1);
      return true;
    case VK_RETURN:
      // Only with a row selected; otherwise the page gets it (submits).
      if (Selectable(selected_)) {
        Finish(selected_);
        return true;
      }
      return false;
    case VK_ESCAPE:
      Close();
      return true;
    default:
      return false;
  }
}

int PasswordDropDown::RowAt(int y) const {
  const int row = (y - Scale(kPadding)) / std::max(1, Scale(kRowHeight));
  return y >= Scale(kPadding) && row < static_cast<int>(items_.size()) ? row
                                                                       : -1;
}

void PasswordDropDown::Paint() {
  PAINTSTRUCT paint = {};
  HDC dc = BeginPaint(window_, &paint);
  RECT client = {};
  GetClientRect(window_, &client);
  SetDCBrushColor(dc, theme_.toolbar);
  FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
  SetBkMode(dc, TRANSPARENT);
  const int row_height = Scale(kRowHeight);
  const int margin = Scale(12);
  const int icon_width = Scale(26);
  for (std::size_t i = 0; i < items_.size(); ++i) {
    const PasswordMenuItem& item = items_[i];
    const bool warning = item.kind == PasswordMenuItem::Kind::kWarning;
    const int top = Scale(kPadding) + static_cast<int>(i) * row_height;
    RECT bounds = {client.left + Scale(4), top, client.right - Scale(4),
                   top + row_height};
    if (static_cast<int>(i) == selected_) {
      SetDCBrushColor(dc, theme_.hover);
      FillRect(dc, &bounds, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    }
    SetTextColor(dc, warning ? WarningColor(theme_) : theme_.text_dim);
    SelectObject(dc, icon_font_);
    RECT icon = {client.left + margin, top, client.left + margin + icon_width,
                 top + row_height};
    DrawTextW(dc, GlyphOf(item.kind), -1, &icon,
              DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(dc, text_font_);
    const std::wstring title = Utf8ToWide(item.title);
    const std::wstring detail = DetailOf(item);
    RECT text = {icon.right, top, client.right - margin, top + row_height};
    SetTextColor(dc, warning ? WarningColor(theme_) : theme_.text);
    if (detail.empty()) {
      DrawTextW(dc, title.c_str(), -1, &text,
                DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
      continue;
    }
    // The title above, the detail (host, password, code) dimmed below.
    RECT upper = {text.left, top + Scale(3), text.right,
                  top + row_height / 2 + Scale(1)};
    RECT lower = {text.left, top + row_height / 2 - Scale(1), text.right,
                  top + row_height - Scale(3)};
    DrawTextW(dc, title.c_str(), -1, &upper,
              DT_SINGLELINE | DT_BOTTOM | DT_NOPREFIX | DT_END_ELLIPSIS);
    SetTextColor(dc, theme_.text_dim);
    DrawTextW(dc, detail.c_str(), -1, &lower,
              DT_SINGLELINE | DT_TOP | DT_NOPREFIX | DT_END_ELLIPSIS);
  }
  EndPaint(window_, &paint);
}

LRESULT CALLBACK PasswordDropDown::WindowProc(HWND window,
                                              UINT message,
                                              WPARAM wparam,
                                              LPARAM lparam) {
  auto* self = reinterpret_cast<PasswordDropDown*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (!self) {
    return DefWindowProcW(window, message, wparam, lparam);
  }
  return self->HandleMessage(message, wparam, lparam);
}

LRESULT PasswordDropDown::HandleMessage(UINT message,
                                        WPARAM wparam,
                                        LPARAM lparam) {
  switch (message) {
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;  // The page keeps the keyboard.
    case WM_ERASEBKGND:
      return 1;  // Painted in WM_PAINT.
    case WM_PAINT:
      Paint();
      return 0;
    case WM_MOUSEMOVE:
      if (const int row = RowAt(GET_Y_LPARAM(lparam)); Selectable(row)) {
        Select(row);
      }
      return 0;
    case WM_LBUTTONUP:
      if (const int row = RowAt(GET_Y_LPARAM(lparam)); Selectable(row)) {
        Finish(row);
      }
      return 0;
    default:
      break;
  }
  return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace brouk::win
