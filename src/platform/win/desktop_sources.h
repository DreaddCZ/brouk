#ifndef BROUK_PLATFORM_WIN_DESKTOP_SOURCES_H_
#define BROUK_PLATFORM_WIN_DESKTOP_SOURCES_H_

#include <cstddef>
#include <string>
#include <vector>

#include "core/screen_share.h"

namespace brouk::win {

// A screen or window the user can share, with its name for the picker.
struct DesktopSourceOption {
  DesktopSource source;
  std::wstring title;
};

// The screens (with Chromium's ids on Windows: the index of the display
// device, as WebRTC's screen list numbers them), then up to `max_windows`
// top-level windows with a title, front-most first. UI thread.
std::vector<DesktopSourceOption> ListDesktopSources(size_t max_windows);

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_DESKTOP_SOURCES_H_
