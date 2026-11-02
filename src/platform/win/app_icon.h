#ifndef BROUK_PLATFORM_WIN_APP_ICON_H_
#define BROUK_PLATFORM_WIN_APP_ICON_H_

#include <windows.h>

namespace brouk::win {

// Gives a window class Brouk's icon (from brouk.exe, put there by
// exe_stamp at build time), for the title bar, the taskbar and Alt+Tab.
// Without it those show Windows' blank app icon.
void SetAppIcon(WNDCLASSEXW& window_class);

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_APP_ICON_H_
