#include "platform/win/tab_drag_image.h"

namespace brouk::win {

namespace {

constexpr wchar_t kWindowClass[] = L"BroukTabDragImage";
constexpr BYTE kOpacity = 230;  // Slightly see-through, as in Chrome.

bool RegisterWindowClass(WNDPROC procedure) {
  static const bool registered = [procedure] {
    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_DROPSHADOW;
    window_class.lpfnWndProc = procedure;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = kWindowClass;
    return RegisterClassExW(&window_class) != 0;
  }();
  return registered;
}

}  // namespace

TabDragImage::TabDragImage(HBITMAP bitmap, SIZE size, POINT origin)
    : bitmap_(bitmap), size_(size) {
  if (!RegisterWindowClass(&TabDragImage::WindowProc)) {
    return;
  }
  // Transparent to the mouse: the window under it gets the hit tests.
  window_ = CreateWindowExW(
      WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
          WS_EX_TOPMOST,
      kWindowClass, L"", WS_POPUP, origin.x, origin.y, size.cx, size.cy,
      nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!window_) {
    return;
  }
  SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  SetLayeredWindowAttributes(window_, 0, kOpacity, LWA_ALPHA);
  ShowWindow(window_, SW_SHOWNOACTIVATE);
  UpdateWindow(window_);
}

TabDragImage::~TabDragImage() {
  if (window_) {
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    DestroyWindow(window_);
  }
  if (bitmap_) {
    DeleteObject(bitmap_);
  }
}

void TabDragImage::MoveTo(POINT origin) {
  if (window_) {
    SetWindowPos(window_, HWND_TOPMOST, origin.x, origin.y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE);
  }
}

// static
LRESULT CALLBACK TabDragImage::WindowProc(HWND window,
                                          UINT message,
                                          WPARAM wparam,
                                          LPARAM lparam) {
  auto* self =
      reinterpret_cast<TabDragImage*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (self && message == WM_PAINT) {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window, &paint);
    HDC source = CreateCompatibleDC(dc);
    HGDIOBJ previous = SelectObject(source, self->bitmap_);
    BitBlt(dc, 0, 0, self->size_.cx, self->size_.cy, source, 0, 0, SRCCOPY);
    SelectObject(source, previous);
    DeleteDC(source);
    EndPaint(window, &paint);
    return 0;
  }
  if (message == WM_NCHITTEST) {
    return HTTRANSPARENT;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace brouk::win
