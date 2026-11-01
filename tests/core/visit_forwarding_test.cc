#include "core/visit_forwarding.h"

#include <gtest/gtest.h>

namespace brouk {
namespace {

VisitArrival Arrival(bool link_or_form,
                     bool user_gesture,
                     std::int64_t started_ms,
                     std::int64_t previous_loaded_ms) {
  return {.link_or_form = link_or_form,
          .user_gesture = user_gesture,
          .started_ms = started_ms,
          .previous_loaded_ms = previous_loaded_ms};
}

TEST(VisitForwardingTest, FormPostedByThePageRightAfterLoading) {
  // A sign-in page posting its answer from its load handler.
  EXPECT_TRUE(ForwardedByItself(Arrival(true, false, 10'200, 10'000)));
  EXPECT_TRUE(ForwardedByItself(
      Arrival(true, false, 10'000 + kForwardWindowMs, 10'000)));
}

TEST(VisitForwardingTest, TheUserStartedIt) {
  EXPECT_FALSE(ForwardedByItself(Arrival(true, true, 10'200, 10'000)));
}

TEST(VisitForwardingTest, LongAfterLoading) {
  EXPECT_FALSE(ForwardedByItself(
      Arrival(true, false, 10'001 + kForwardWindowMs, 10'000)));
}

TEST(VisitForwardingTest, NotFromALinkOrForm) {
  // Typed, bookmarks, reloads and back/forward never remove a page.
  EXPECT_FALSE(ForwardedByItself(Arrival(false, false, 10'200, 10'000)));
}

TEST(VisitForwardingTest, NoPageBefore) {
  EXPECT_FALSE(ForwardedByItself(Arrival(true, false, 10'200, 0)));
  // Started before the previous page had loaded: Chromium's own client
  // redirect qualifier covers that case.
  EXPECT_FALSE(ForwardedByItself(Arrival(true, false, 9'000, 10'000)));
}

}  // namespace
}  // namespace brouk
