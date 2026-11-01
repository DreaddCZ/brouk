#include "platform/win/devtools_frame.h"

#include <utility>

#include "platform/win/theme.h"

namespace brouk::win {

namespace {

constexpr wchar_t kWindowClass[] = L"BroukDevToolsFrame";

bool RegisterWindowClass() {
  static const bool registered = [] {
    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(GetModuleHandleW(nullptr), IDI_APPLICATION);
    window_class.lpszClassName = kWindowClass;
    return RegisterClassExW(&window_class) != 0;
  }();
  return registered;
}

}  // namespace

DevToolsFrame::DevToolsFrame(HWND beside, std::function<void()> close_requested)
    : close_requested_(std::move(close_requested)) {
  if (!RegisterWindowClass()) {
    return;
  }
  const int dpi = static_cast<int>(GetDpiForWindow(beside));
  RECT around = {};
  GetWindowRect(beside, &around);
  // Down and right of the browser window, as macOS cascades it.
  window_ = CreateWindowExW(0, kWindowClass, L"Developer Tools",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            around.left + MulDiv(60, dpi, 96),
                            around.top + MulDiv(60, dpi, 96),
                            MulDiv(900, dpi, 96), MulDiv(600, dpi, 96), nullptr,
                            nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!window_) {
    return;
  }
  SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  SetWindowLongPtrW(window_, GWLP_WNDPROC,
                    reinterpret_cast<LONG_PTR>(&DevToolsFrame::WindowProc));
  ApplyThemeToFrame(window_, CurrentTheme());
}

DevToolsFrame::~DevToolsFrame() {
  if (window_) {
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    DestroyWindow(window_);
  }
}

void DevToolsFrame::SetTitle(const std::wstring& title) {
  if (window_) {
    SetWindowTextW(window_, title.c_str());
  }
}

void DevToolsFrame::Show(HWND child) {
  if (!window_) {
    return;
  }
  child_ = child;
  SetParent(child_, window_);
  RECT client = {};
  GetClientRect(window_, &client);
  SetWindowPos(child_, nullptr, 0, 0, client.right, client.bottom,
               SWP_NOZORDER | SWP_SHOWWINDOW);
  ShowWindow(window_, SW_SHOW);
  SetForegroundWindow(window_);
}

LRESULT CALLBACK DevToolsFrame::WindowProc(HWND window,
                                           UINT message,
                                           WPARAM wparam,
                                           LPARAM lparam) {
  auto* self = reinterpret_cast<DevToolsFrame*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (!self) {
    return DefWindowProcW(window, message, wparam, lparam);
  }
  switch (message) {
    case WM_SIZE:
      if (self->child_ && IsWindow(self->child_)) {
        SetWindowPos(self->child_, nullptr, 0, 0, LOWORD(lparam),
                     HIWORD(lparam), SWP_NOZORDER | SWP_NOACTIVATE);
      }
      return 0;
    case WM_SETTINGCHANGE:
      ApplyThemeToFrame(window, CurrentTheme());
      break;
    case WM_CLOSE:
      // The dock closes its browsers first; the owner deletes the frame.
      if (self->close_requested_) {
        self->close_requested_();
      }
      return 0;
    default:
      break;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace brouk::win
