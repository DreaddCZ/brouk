#include "core/window_frame.h"

#include <gtest/gtest.h>

namespace brouk {
namespace {

// A laptop display below an ultra-wide one, as macOS reports them (y up).
constexpr FrameRect kLaptop{0, 0, 1512, 944};

TEST(WindowFrameTest, FrameInsideStaysAsItIs) {
  FrameRect frame{100, 50, 800, 600};
  EXPECT_EQ(FitFrameToArea(frame, kLaptop, 480, 300), frame);
}

TEST(WindowFrameTest, TooLargeFrameShrinksToTheArea) {
  // Moved from a 3440 x 1440 display with its size kept.
  EXPECT_EQ(FitFrameToArea({-200, -100, 2400, 1300}, kLaptop, 480, 300),
            (FrameRect{0, 0, 1512, 944}));
}

TEST(WindowFrameTest, FrameOutsideMovesIn) {
  // Top edge above the area, right edge past it.
  EXPECT_EQ(FitFrameToArea({1000, 600, 800, 600}, kLaptop, 480, 300),
            (FrameRect{712, 344, 800, 600}));
  // Below and left of it.
  EXPECT_EQ(FitFrameToArea({-300, -200, 800, 600}, kLaptop, 480, 300),
            (FrameRect{0, 0, 800, 600}));
}

TEST(WindowFrameTest, MinimumSizeWinsOverASmallArea) {
  // The window hangs out at the bottom, its top edge stays visible.
  FrameRect fitted =
      FitFrameToArea({0, 0, 800, 600}, {0, 0, 400, 200}, 480, 300);
  EXPECT_EQ(fitted, (FrameRect{0, -100, 480, 300}));
}

TEST(WindowFrameTest, AreaNotAtTheOrigin) {
  // A second display to the right, its usable part under the menu bar.
  constexpr FrameRect kRight{1512, -300, 3440, 1410};
  EXPECT_EQ(FitFrameToArea({1400, 1000, 1000, 800}, kRight, 480, 300),
            (FrameRect{1512, 310, 1000, 800}));
}

}  // namespace
}  // namespace brouk
