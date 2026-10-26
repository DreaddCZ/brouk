#include "core/window_frame.h"

#include <algorithm>

namespace brouk {

namespace {

// Places a span of `size` inside [start, start + extent]; if it does not
// fit, at `start`.
double Place(double position, double size, double start, double extent) {
  return std::max(start, std::min(position, start + extent - size));
}

}  // namespace

FrameRect FitFrameToArea(const FrameRect& frame,
                         const FrameRect& area,
                         double min_width,
                         double min_height) {
  FrameRect fitted = frame;
  fitted.width = std::max(min_width, std::min(frame.width, area.width));
  fitted.height = std::max(min_height, std::min(frame.height, area.height));
  fitted.x = Place(frame.x, fitted.width, area.x, area.width);
  // A window taller than the area keeps its top edge (the tab strip, which
  // moves it) inside: it then hangs out at the bottom.
  double top = std::min(frame.y + frame.height, area.y + area.height);
  top = std::max(top, area.y + std::min(fitted.height, area.height));
  fitted.y = top - fitted.height;
  return fitted;
}

}  // namespace brouk
