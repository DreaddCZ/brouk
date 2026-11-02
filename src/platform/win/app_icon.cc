#include "platform/win/app_icon.h"

namespace brouk::win {

namespace {

// RT_GROUP_ICON as a wide resource type (it follows UNICODE, which Brouk
// does not define).
const LPCWSTR kGroupIconType = MAKEINTRESOURCEW(14);

BOOL CALLBACK TakeFirst(HMODULE /*module*/,
                        LPCWSTR /*type*/,
                        LPWSTR name,
                        LONG_PTR param) {
  *reinterpret_cast<LPWSTR*>(param) = name;
  return FALSE;  // Stops at the first group, the one Windows shows.
}

// One of the app's icons at the system's size `metric` (SM_CXICON or
// SM_CXSMICON). LR_SHARED: the system owns it and keeps it for the
// process's lifetime.
HICON LoadAppIcon(int metric) {
  HMODULE exe = GetModuleHandleW(nullptr);
  // A numeric name, or a string owned by the module, which stays loaded.
  LPWSTR name = nullptr;
  EnumResourceNamesW(exe, kGroupIconType, &TakeFirst,
                     reinterpret_cast<LONG_PTR>(&name));
  if (name == nullptr) {
    return nullptr;
  }
  const int size = GetSystemMetrics(metric);
  return static_cast<HICON>(
      LoadImageW(exe, name, IMAGE_ICON, size, size, LR_SHARED));
}

}  // namespace

void SetAppIcon(WNDCLASSEXW& window_class) {
  // ("small" is a macro in the Windows headers.)
  static const HICON large_icon = LoadAppIcon(SM_CXICON);
  static const HICON small_icon = LoadAppIcon(SM_CXSMICON);
  window_class.hIcon = large_icon;
  window_class.hIconSm = small_icon;
}

}  // namespace brouk::win
