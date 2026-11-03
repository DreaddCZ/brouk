#ifndef BROUK_CORE_DEBUG_LOGGING_H_
#define BROUK_CORE_DEBUG_LOGGING_H_

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace brouk {

// When debug logging (025) is on: it is switched on with the setting
// logging.debug, stays on across restarts, and turns itself off after
// logging.debug_hours, so it is not left on by mistake. The end time is
// written to logging.debug_until when it is switched on.
struct DebugLoggingSettings {
  bool debug = false;
  std::string until;  // logging.debug_until; empty when not set.
  std::int64_t hours = 24;
};

struct DebugLoggingDecision {
  bool on = false;
  // Settings to write: debug off once the time is over, the end time set
  // when it is switched on, cleared when it is off.
  std::optional<bool> write_debug;
  std::optional<std::string> write_until;
  // While on: when to decide again (the end time).
  std::optional<std::chrono::system_clock::duration> recheck_in;
};

DebugLoggingDecision DecideDebugLogging(
    const DebugLoggingSettings& settings,
    std::chrono::system_clock::time_point now);

// UTC times as written to the settings file: "2026-11-04T10:30:00Z".
std::string FormatUtcTime(std::chrono::system_clock::time_point time);
std::optional<std::chrono::system_clock::time_point> ParseUtcTime(
    std::string_view text);

}  // namespace brouk

#endif  // BROUK_CORE_DEBUG_LOGGING_H_
