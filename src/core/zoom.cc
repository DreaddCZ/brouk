#include "core/zoom.h"

#include <cmath>

namespace brouk {

int ZoomPercentFromLevel(double level) {
  return static_cast<int>(std::lround(100.0 * std::pow(1.2, level)));
}

bool IsZoomIndicatorShown(double level) {
  return ZoomPercentFromLevel(level) != 100;
}

}  // namespace brouk
