#include "platform/win/undo_notice.h"

#include <windowsx.h>

#include <algorithm>
#include <utility>

namespace brouk::win {

namespace {

constexpr wchar_t kWindowClass[] = L"BroukUndoNotice";
constexpr UINT_PTR kCloseTimer = 1;
constexpr UINT kShownMilliseconds = 6000;  // As on macOS.

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

SIZE TextSize(HDC dc, const std::wstring& text) {
  RECT measured = {};
  DrawTextW(dc, text.c_str(), -1, &measured,
            DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
  return {measured.right - measured.left, measured.bottom - measured.top};
}

}  // namespace

UndoNotice::~UndoNotice() {
  if (window_) {
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    DestroyWindow(window_);
  }
}

int UndoNotice::Scale(int dips) const {
  return MulDiv(dips, static_cast<int>(GetDpiForWindow(owner_)), 96);
}

void UndoNotice::Show(HWND owner,
                      const RECT& anchor,
                      std::wstring message,
                      std::wstring action,
                      std::function<void()> run,
                      const Theme& theme,
                      HFONT font) {
  if (!RegisterWindowClass()) {
    return;
  }
  owner_ = owner;
  message_ = std::move(message);
  action_ = std::move(action);
  run_ = std::move(run);
  theme_ = theme;
  font_ = font;
  if (!window_) {
    // Never activated: the page or the address bar keeps the keyboard.
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kWindowClass,
                              L"Undo", WS_POPUP | WS_BORDER, 0, 0, 0, 0, owner_,
                              nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window_) {
      return;
    }
    SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetWindowLongPtrW(window_, GWLP_WNDPROC,
                      reinterpret_cast<LONG_PTR>(&UndoNotice::WindowProc));
  }
  // Sized to the texts: margin, message, gap, action button, margin.
  HDC dc = GetDC(window_);
  HGDIOBJ old_font = SelectObject(dc, font_);
  const SIZE message_size = TextSize(dc, message_);
  const SIZE action_size = TextSize(dc, action_);
  SelectObject(dc, old_font);
  ReleaseDC(window_, dc);
  const int margin = Scale(14);
  const int button_padding = Scale(10);
  const int height = std::max<int>(message_size.cy, action_size.cy) + Scale(24);
  const int button_width = action_size.cx + 2 * button_padding;
  const int width =
      margin + message_size.cx + Scale(16) + button_width + Scale(8);
  action_bounds_ = {width - Scale(8) - button_width, Scale(6), width - Scale(8),
                    height - Scale(6)};
  int x = anchor.right - width;
  int y = anchor.bottom + Scale(4);
  MONITORINFO monitor = {.cbSize = sizeof(MONITORINFO),
                         .rcMonitor = {},
                         .rcWork = {},
                         .dwFlags = 0};
  if (GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST),
                      &monitor)) {
    const int left = static_cast<int>(monitor.rcWork.left);
    const int right = static_cast<int>(monitor.rcWork.right);
    x = std::clamp(x, left, std::max(left, right - width));
  }
  // Borders are outside the client area.
  RECT frame = {0, 0, width, height};
  AdjustWindowRectEx(&frame, WS_POPUP | WS_BORDER, FALSE,
                     WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
  SetWindowPos(window_, HWND_TOP, x, y, frame.right - frame.left,
               frame.bottom - frame.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  InvalidateRect(window_, nullptr, FALSE);
  SetTimer(window_, kCloseTimer, kShownMilliseconds, nullptr);
}

void UndoNotice::Close() {
  if (window_) {
    KillTimer(window_, kCloseTimer);
    ShowWindow(window_, SW_HIDE);
  }
  run_ = nullptr;
}

void UndoNotice::Paint() {
  PAINTSTRUCT paint = {};
  HDC dc = BeginPaint(window_, &paint);
  RECT client = {};
  GetClientRect(window_, &client);
  SetDCBrushColor(dc, theme_.toolbar);
  FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
  SetBkMode(dc, TRANSPARENT);
  HGDIOBJ old_font = SelectObject(dc, font_);
  RECT message = {Scale(14), 0, action_bounds_.left, client.bottom};
  SetTextColor(dc, theme_.text);
  DrawTextW(dc, message_.c_str(), -1, &message,
            DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
  // The action as a button in the system's accent colour, as the default
  // buttons of Brouk's panels.
  const COLORREF accent = GetSysColor(COLOR_HIGHLIGHT);
  SetDCBrushColor(dc, action_hovered_ ? GetSysColor(COLOR_HOTLIGHT) : accent);
  HGDIOBJ old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
  HGDIOBJ old_brush = SelectObject(dc, GetStockObject(DC_BRUSH));
  RoundRect(dc, action_bounds_.left, action_bounds_.top,
            action_bounds_.right + 1, action_bounds_.bottom + 1, Scale(6),
            Scale(6));
  SelectObject(dc, old_brush);
  SelectObject(dc, old_pen);
  SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
  RECT action = action_bounds_;
  DrawTextW(dc, action_.c_str(), -1, &action,
            DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
  SelectObject(dc, old_font);
  EndPaint(window_, &paint);
}

LRESULT CALLBACK UndoNotice::WindowProc(HWND window,
                                        UINT message,
                                        WPARAM wparam,
                                        LPARAM lparam) {
  auto* self =
      reinterpret_cast<UndoNotice*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (!self) {
    return DefWindowProcW(window, message, wparam, lparam);
  }
  return self->HandleMessage(message, wparam, lparam);
}

LRESULT UndoNotice::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case WM_TIMER:
      if (wparam == kCloseTimer) {
        Close();
        return 0;
      }
      break;
    case WM_ERASEBKGND:
      return 1;  // Painted in WM_PAINT.
    case WM_PAINT:
      Paint();
      return 0;
    case WM_MOUSEMOVE: {
      const POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      const bool hovered = PtInRect(&action_bounds_, point) != FALSE;
      if (hovered != action_hovered_) {
        action_hovered_ = hovered;
        InvalidateRect(window_, nullptr, FALSE);
        TRACKMOUSEEVENT track = {.cbSize = sizeof(TRACKMOUSEEVENT),
                                 .dwFlags = TME_LEAVE,
                                 .hwndTrack = window_,
                                 .dwHoverTime = 0};
        TrackMouseEvent(&track);
      }
      return 0;
    }
    case WM_MOUSELEAVE:
      action_hovered_ = false;
      InvalidateRect(window_, nullptr, FALSE);
      return 0;
    case WM_LBUTTONUP: {
      const POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      if (PtInRect(&action_bounds_, point)) {
        // Closed first: the action may show another notice.
        std::function<void()> run = std::move(run_);
        Close();
        if (run) {
          run();
        }
      }
      return 0;
    }
    default:
      break;
  }
  return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace brouk::win
