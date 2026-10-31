#include "platform/win/theme.h"

#include <dwmapi.h>

namespace brouk::win {

namespace {

// HKCU\...\Themes\Personalize\AppsUseLightTheme: 0 in dark mode.
bool SystemUsesDarkMode() {
  DWORD light = 1;
  DWORD size = sizeof(light);
  if (RegGetValueW(
          HKEY_CURRENT_USER,
          L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
          L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light,
          &size) != ERROR_SUCCESS) {
    return false;
  }
  return light == 0;
}

ThemeChoice& Choice() {
  static ThemeChoice choice = ThemeChoice::kSystem;
  return choice;
}

}  // namespace

void SetThemeChoice(ThemeChoice choice) {
  Choice() = choice;
}

std::string_view PageColorScheme() {
  switch (Choice()) {
    case ThemeChoice::kLight:
      return "light";
    case ThemeChoice::kDark:
      return "dark";
    case ThemeChoice::kSystem:
      break;
  }
  return {};
}

Theme CurrentTheme() {
  Theme theme;
  switch (Choice()) {
    case ThemeChoice::kSystem:
      theme.dark = SystemUsesDarkMode();
      break;
    case ThemeChoice::kLight:
      theme.dark = false;
      break;
    case ThemeChoice::kDark:
      theme.dark = true;
      break;
  }
  // Close to Chrome's and Edge's default colours on Windows 11.
  if (theme.dark) {
    theme.frame = RGB(32, 32, 32);
    theme.frame_inactive = RGB(43, 43, 43);
    theme.toolbar = RGB(53, 54, 58);
    theme.text = RGB(232, 234, 237);
    theme.text_dim = RGB(170, 172, 176);
    theme.hover = RGB(66, 67, 71);
    theme.separator = RGB(80, 80, 84);
    theme.field = RGB(32, 33, 36);
  } else {
    theme.frame = RGB(222, 225, 230);
    theme.frame_inactive = RGB(235, 237, 240);
    theme.toolbar = RGB(255, 255, 255);
    theme.text = RGB(32, 33, 36);
    theme.text_dim = RGB(95, 99, 104);
    theme.hover = RGB(235, 236, 239);
    theme.separator = RGB(160, 163, 168);
    theme.field = RGB(241, 243, 244);
  }
  theme.close_hover = RGB(196, 43, 28);
  return theme;
}

void ApplyThemeToFrame(HWND window, const Theme& theme) {
  // DWMWA_USE_IMMERSIVE_DARK_MODE (20): Windows 10 2004 and later.
  constexpr DWORD kUseImmersiveDarkMode = 20;
  const BOOL dark = theme.dark ? TRUE : FALSE;
  DwmSetWindowAttribute(window, kUseImmersiveDarkMode, &dark, sizeof(dark));
}

}  // namespace brouk::win
