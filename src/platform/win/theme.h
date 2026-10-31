#ifndef BROUK_PLATFORM_WIN_THEME_H_
#define BROUK_PLATFORM_WIN_THEME_H_

#include <windows.h>

#include <string_view>

namespace brouk::win {

// The colours Brouk draws its own window parts with (title bar, tabs,
// toolbar), following the system's light or dark app mode (024). Web pages
// follow it through Chromium.
struct Theme {
  bool dark = false;
  COLORREF frame = 0;           // Title bar and inactive tabs' background.
  COLORREF frame_inactive = 0;  // The same while the window is inactive.
  COLORREF toolbar = 0;         // Active tab and toolbar.
  COLORREF text = 0;
  COLORREF text_dim = 0;     // Inactive tabs' titles.
  COLORREF hover = 0;        // Hovered tab and buttons.
  COLORREF separator = 0;    // Between inactive tabs.
  COLORREF close_hover = 0;  // The window's close button (red, as Windows).
  COLORREF field = 0;        // The address bar's background.
};

// The user's choice (appearance.theme, 004): the system's app mode
// (Settings > Personalisation > Colours > "Choose your app mode"), or
// always light or dark. UI thread.
enum class ThemeChoice { kSystem, kLight, kDark };
void SetThemeChoice(ThemeChoice choice);
// For web pages' prefers-color-scheme: "light", "dark", or empty for the
// system's (EmulateColorScheme).
std::string_view PageColorScheme();

// The theme for the user's choice.
Theme CurrentTheme();

// Tells Windows to draw the window's border and shadow for the theme (dark
// border in dark mode on Windows 11).
void ApplyThemeToFrame(HWND window, const Theme& theme);

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_THEME_H_
