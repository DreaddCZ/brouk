#include "platform/win/system_events.h"

#include <wtsapi32.h>

#include <utility>

namespace brouk::win {

namespace {

constexpr wchar_t kWindowClass[] = L"BroukSystemEvents";

bool RegisterWindowClass() {
  static const bool registered = [] {
    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = kWindowClass;
    return RegisterClassExW(&window_class) != 0;
  }();
  return registered;
}

}  // namespace

SystemEvents::SystemEvents(std::function<void()> sleep_or_lock)
    : sleep_or_lock_(std::move(sleep_or_lock)) {
  if (!RegisterWindowClass()) {
    return;
  }
  // A hidden top-level window: message-only windows get no broadcasts.
  window_ =
      CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"", WS_POPUP, 0, 0, 0, 0,
                      nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!window_) {
    return;
  }
  SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  SetWindowLongPtrW(window_, GWLP_WNDPROC,
                    reinterpret_cast<LONG_PTR>(&SystemEvents::WindowProc));
  session_notifications_ =
      WTSRegisterSessionNotification(window_, NOTIFY_FOR_THIS_SESSION) != FALSE;
}

SystemEvents::~SystemEvents() {
  if (window_) {
    if (session_notifications_) {
      WTSUnRegisterSessionNotification(window_);
    }
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    DestroyWindow(window_);
  }
}

LRESULT CALLBACK SystemEvents::WindowProc(HWND window,
                                          UINT message,
                                          WPARAM wparam,
                                          LPARAM lparam) {
  auto* self =
      reinterpret_cast<SystemEvents*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  const bool sleeping =
      message == WM_POWERBROADCAST && wparam == PBT_APMSUSPEND;
  const bool locked =
      message == WM_WTSSESSION_CHANGE && wparam == WTS_SESSION_LOCK;
  if (self && (sleeping || locked) && self->sleep_or_lock_) {
    self->sleep_or_lock_();
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace brouk::win
