#include "platform/win/desktop_sources.h"

#include <windows.h>

#include <dwmapi.h>

#include <cstdint>
#include <utility>

#include "platform/win/controls.h"

namespace brouk::win {

namespace {

constexpr size_t kMaxTitle = 60;

struct WindowSearch {
  size_t max_windows = 0;
  std::vector<DesktopSourceOption>* options = nullptr;
};

// Windows the user sees on the taskbar: visible, not cloaked (other virtual
// desktops, suspended apps), unowned, not tool windows, with a title.
bool IsShareableWindow(HWND window) {
  if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) ||
      (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0 ||
      GetWindowTextLengthW(window) == 0) {
    return false;
  }
  DWORD cloaked = 0;
  if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked,
                                      sizeof(cloaked))) &&
      cloaked != 0) {
    return false;
  }
  return true;
}

BOOL CALLBACK AddWindow(HWND window, LPARAM data) {
  auto* search = reinterpret_cast<WindowSearch*>(data);
  if (!IsShareableWindow(window)) {
    return TRUE;
  }
  std::wstring title = WindowText(window);
  if (title.size() > kMaxTitle) {
    title = title.substr(0, kMaxTitle - 1) + L"\u2026";
  }
  // Window handles fit in 32 bits, also in 64-bit processes; Chromium's
  // window ids are the handle's value.
  const auto id =
      static_cast<std::uint32_t>(reinterpret_cast<uintptr_t>(window));
  search->options->push_back(
      {DesktopSource{DesktopSource::Type::kWindow, id}, title});
  return search->options->size() < search->max_windows ? TRUE : FALSE;
}

}  // namespace

std::vector<DesktopSourceOption> ListDesktopSources(size_t max_windows) {
  std::vector<DesktopSourceOption> screens;
  // WebRTC's screen list: the display devices attached to the desktop,
  // numbered by their index among all display devices.
  DISPLAY_DEVICEW device = {};
  device.cb = sizeof(device);
  for (DWORD index = 0; EnumDisplayDevicesW(nullptr, index, &device, 0);
       ++index) {
    if ((device.StateFlags & DISPLAY_DEVICE_ACTIVE) != 0) {
      DEVMODEW mode = {};
      mode.dmSize = sizeof(mode);
      std::wstring size;
      if (EnumDisplaySettingsW(device.DeviceName, ENUM_CURRENT_SETTINGS,
                               &mode)) {
        size = L" (" + std::to_wstring(mode.dmPelsWidth) + L" \u00D7 " +
               std::to_wstring(mode.dmPelsHeight) + L")";
      }
      screens.push_back(
          {DesktopSource{DesktopSource::Type::kScreen, index}, size});
    }
    device = {};
    device.cb = sizeof(device);
  }
  std::vector<DesktopSourceOption> options;
  for (size_t i = 0; i < screens.size(); ++i) {
    const std::wstring name = screens.size() == 1
                                  ? std::wstring(L"Entire screen")
                                  : L"Screen " + std::to_wstring(i + 1);
    options.push_back({screens[i].source, name + screens[i].title});
  }
  std::vector<DesktopSourceOption> windows;
  if (max_windows > 0) {
    WindowSearch search{max_windows, &windows};
    EnumWindows(&AddWindow, reinterpret_cast<LPARAM>(&search));
  }
  for (DesktopSourceOption& window : windows) {
    window.title = L"Window: " + window.title;
    options.push_back(std::move(window));
  }
  return options;
}

}  // namespace brouk::win
