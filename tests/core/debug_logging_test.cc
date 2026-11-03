#include "core/debug_logging.h"

#include <chrono>

#include <gtest/gtest.h>

namespace brouk {
namespace {

using std::chrono::hours;
using std::chrono::system_clock;

system_clock::time_point Time(const char* text) {
  return ParseUtcTime(text).value();
}

TEST(DebugLoggingTest, UtcTimesReadBack) {
  const system_clock::time_point time = Time("2026-11-04T10:30:05Z");
  EXPECT_EQ(FormatUtcTime(time), "2026-11-04T10:30:05Z");
  EXPECT_EQ(FormatUtcTime(system_clock::time_point()), "1970-01-01T00:00:00Z");
  for (const char* wrong :
       {"", "2026-11-04", "2026-13-01T00:00:00Z", "2026-02-30T00:00:00Z",
        "2026-11-04T24:00:00Z", "2026-11-04T10:30:05", "2026-11-04T10:30:05Zx",
        "yesterday"}) {
    EXPECT_FALSE(ParseUtcTime(wrong)) << wrong;
  }
}

TEST(DebugLoggingTest, OffClearsTheEndTime) {
  const DebugLoggingDecision off =
      DecideDebugLogging({.debug = false}, Time("2026-11-04T10:00:00Z"));
  EXPECT_FALSE(off.on);
  EXPECT_FALSE(off.write_until);
  const DebugLoggingDecision stale =
      DecideDebugLogging({.debug = false, .until = "2026-11-05T10:00:00Z"},
                         Time("2026-11-04T10:00:00Z"));
  EXPECT_FALSE(stale.on);
  EXPECT_EQ(stale.write_until, "");
}

TEST(DebugLoggingTest, SwitchingOnSetsTheEndTime) {
  const DebugLoggingDecision decision = DecideDebugLogging(
      {.debug = true, .hours = 24}, Time("2026-11-04T10:00:00Z"));
  EXPECT_TRUE(decision.on);
  EXPECT_EQ(decision.write_until, "2026-11-05T10:00:00Z");
  EXPECT_FALSE(decision.write_debug);
  EXPECT_EQ(decision.recheck_in, hours(24));
}

TEST(DebugLoggingTest, StaysOnUntilTheEndTimeAcrossRestarts) {
  const DebugLoggingSettings settings = {
      .debug = true, .until = "2026-11-05T10:00:00Z", .hours = 24};
  const DebugLoggingDecision later =
      DecideDebugLogging(settings, Time("2026-11-05T09:00:00Z"));
  EXPECT_TRUE(later.on);
  EXPECT_FALSE(later.write_until);
  EXPECT_EQ(later.recheck_in, hours(1));
  const DebugLoggingDecision over =
      DecideDebugLogging(settings, Time("2026-11-05T10:00:00Z"));
  EXPECT_FALSE(over.on);
  EXPECT_EQ(over.write_debug, false);
  EXPECT_EQ(over.write_until, "");
}

TEST(DebugLoggingTest, AnEditedEndTimeThatIsNotATimeStartsAgain) {
  const DebugLoggingDecision decision =
      DecideDebugLogging({.debug = true, .until = "soon", .hours = 2},
                         Time("2026-11-04T10:00:00Z"));
  EXPECT_TRUE(decision.on);
  EXPECT_EQ(decision.write_until, "2026-11-04T12:00:00Z");
}

}  // namespace
}  // namespace brouk
