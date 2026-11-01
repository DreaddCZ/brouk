#ifndef BROUK_PLATFORM_WIN_SYSTEM_EVENTS_H_
#define BROUK_PLATFORM_WIN_SYSTEM_EVENTS_H_

#include <windows.h>

#include <functional>

namespace brouk::win {

// Tells when the PC goes to sleep or the user's session is locked, so
// Bitwarden locks (008), as on macOS when the Mac sleeps or the screen
// locks. A hidden window receives the system's notifications (power
// broadcasts and session changes). UI thread.
class SystemEvents {
 public:
  explicit SystemEvents(std::function<void()> sleep_or_lock);
  ~SystemEvents();
  SystemEvents(const SystemEvents&) = delete;
  SystemEvents& operator=(const SystemEvents&) = delete;

 private:
  static LRESULT CALLBACK WindowProc(HWND window,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam);

  std::function<void()> sleep_or_lock_;
  HWND window_ = nullptr;
  bool session_notifications_ = false;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_SYSTEM_EVENTS_H_
