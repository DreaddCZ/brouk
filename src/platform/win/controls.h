#ifndef BROUK_PLATFORM_WIN_CONTROLS_H_
#define BROUK_PLATFORM_WIN_CONTROLS_H_

#include <windows.h>

#include <string>

namespace brouk::win {

// Small helpers for Win32 controls, shared by the windows and panels.

std::wstring WindowText(HWND window);

// Whether `key` (VK_CONTROL, ...) is held down now.
bool KeyDown(int key);

// The name screen readers announce for a control whose text is an icon
// glyph or that has no text of its own (MSAA annotation, also read through
// UI Automation).
void SetAccessibleName(HWND control, const wchar_t* name);

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_CONTROLS_H_
