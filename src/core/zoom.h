#ifndef BROUK_CORE_ZOOM_H_
#define BROUK_CORE_ZOOM_H_

namespace brouk {

// Converts Chromium's zoom level (0 = 100 %, each step is a factor of 1.2) to
// the whole percentage Chrome shows, e.g. 0.0 -> 100, 1.0 -> 120, -1.0 -> 83.
int ZoomPercentFromLevel(double level);

// Whether the address bar shows a zoom indicator (zoom is not 100 %).
bool IsZoomIndicatorShown(double level);

}  // namespace brouk

#endif  // BROUK_CORE_ZOOM_H_
