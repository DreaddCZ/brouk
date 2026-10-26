#ifndef BROUK_CORE_WINDOW_FRAME_H_
#define BROUK_CORE_WINDOW_FRAME_H_

namespace brouk {

// A rectangle in screen points. On macOS y grows upwards (y is the bottom
// edge).
struct FrameRect {
  double x = 0;
  double y = 0;
  double width = 0;
  double height = 0;

  bool operator==(const FrameRect&) const = default;
};

// Where a window goes when macOS moves it to another display (another
// Space, a display disconnected): `frame` shrunk to `area` (the display's
// usable part) if larger, but not below the window's minimum size, and moved
// so that it lies inside it, the top edge first. A frame already inside is
// returned as it is.
FrameRect FitFrameToArea(const FrameRect& frame,
                         const FrameRect& area,
                         double min_width,
                         double min_height);

}  // namespace brouk

#endif  // BROUK_CORE_WINDOW_FRAME_H_
