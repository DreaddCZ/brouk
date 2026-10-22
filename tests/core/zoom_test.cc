#include "core/zoom.h"

#include <cmath>

#include <gtest/gtest.h>

namespace brouk {
namespace {

// Chrome's preset zoom factors, as Chromium zoom levels (log base 1.2).
double LevelForFactor(double factor) {
  return std::log(factor) / std::log(1.2);
}

TEST(ZoomTest, PercentFromLevel) {
  EXPECT_EQ(ZoomPercentFromLevel(0.0), 100);
  EXPECT_EQ(ZoomPercentFromLevel(1.0), 120);
  EXPECT_EQ(ZoomPercentFromLevel(-1.0), 83);
  EXPECT_EQ(ZoomPercentFromLevel(LevelForFactor(1.1)), 110);
  EXPECT_EQ(ZoomPercentFromLevel(LevelForFactor(0.67)), 67);
  EXPECT_EQ(ZoomPercentFromLevel(LevelForFactor(5.0)), 500);
}

TEST(ZoomTest, IndicatorOnlyWhenNotDefault) {
  EXPECT_FALSE(IsZoomIndicatorShown(0.0));
  EXPECT_FALSE(IsZoomIndicatorShown(1e-9));
  EXPECT_TRUE(IsZoomIndicatorShown(LevelForFactor(1.1)));
  EXPECT_TRUE(IsZoomIndicatorShown(LevelForFactor(0.9)));
}

}  // namespace
}  // namespace brouk
