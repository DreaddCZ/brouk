#include "core/debug_logging.h"

#include <array>
#include <cstdio>

namespace brouk {

namespace {

// The `digits`-digit number at `text[*at]`, moved past; nullopt if not
// digits.
std::optional<int> Number(std::string_view text, std::size_t* at, int digits) {
  if (*at + static_cast<std::size_t>(digits) > text.size()) {
    return std::nullopt;
  }
  int value = 0;
  for (int i = 0; i < digits; ++i) {
    const char c = text[*at + static_cast<std::size_t>(i)];
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
    value = value * 10 + (c - '0');
  }
  *at += static_cast<std::size_t>(digits);
  return value;
}

bool Expect(std::string_view text, std::size_t* at, char c) {
  if (*at < text.size() && text[*at] == c) {
    ++*at;
    return true;
  }
  return false;
}

}  // namespace

std::string FormatUtcTime(std::chrono::system_clock::time_point time) {
  using namespace std::chrono;
  const sys_days day = floor<days>(time);
  const year_month_day date(day);
  const hh_mm_ss<seconds> clock(floor<seconds>(time - day));
  std::array<char, 32> text = {};
  std::snprintf(text.data(), text.size(), "%04d-%02u-%02uT%02d:%02d:%02dZ",
                static_cast<int>(date.year()),
                static_cast<unsigned>(date.month()),
                static_cast<unsigned>(date.day()),
                static_cast<int>(clock.hours().count()),
                static_cast<int>(clock.minutes().count()),
                static_cast<int>(clock.seconds().count()));
  return text.data();
}

std::optional<std::chrono::system_clock::time_point> ParseUtcTime(
    std::string_view text) {
  using namespace std::chrono;
  std::size_t at = 0;
  const std::optional<int> y = Number(text, &at, 4);
  if (!y || !Expect(text, &at, '-')) {
    return std::nullopt;
  }
  const std::optional<int> mo = Number(text, &at, 2);
  if (!mo || !Expect(text, &at, '-')) {
    return std::nullopt;
  }
  const std::optional<int> d = Number(text, &at, 2);
  if (!d || !Expect(text, &at, 'T')) {
    return std::nullopt;
  }
  const std::optional<int> h = Number(text, &at, 2);
  if (!h || !Expect(text, &at, ':')) {
    return std::nullopt;
  }
  const std::optional<int> mi = Number(text, &at, 2);
  if (!mi || !Expect(text, &at, ':')) {
    return std::nullopt;
  }
  const std::optional<int> s = Number(text, &at, 2);
  if (!s || !Expect(text, &at, 'Z') || at != text.size()) {
    return std::nullopt;
  }
  const year_month_day date{year(*y), month(static_cast<unsigned>(*mo)),
                            day(static_cast<unsigned>(*d))};
  if (!date.ok() || *h > 23 || *mi > 59 || *s > 59) {
    return std::nullopt;
  }
  return time_point_cast<system_clock::duration>(sys_days(date) + hours(*h) +
                                                 minutes(*mi) + seconds(*s));
}

DebugLoggingDecision DecideDebugLogging(
    const DebugLoggingSettings& settings,
    std::chrono::system_clock::time_point now) {
  DebugLoggingDecision decision;
  if (!settings.debug) {
    if (!settings.until.empty()) {
      decision.write_until = std::string();
    }
    return decision;
  }
  std::optional<std::chrono::system_clock::time_point> until =
      ParseUtcTime(settings.until);
  if (!until) {
    // Just switched on (or the time was edited into nonsense): from now.
    const std::int64_t hours = settings.hours > 0 ? settings.hours : 1;
    until = now + std::chrono::hours(hours);
    decision.write_until = FormatUtcTime(*until);
  }
  if (*until <= now) {
    decision.write_debug = false;
    decision.write_until = std::string();
    return decision;
  }
  decision.on = true;
  decision.recheck_in = *until - now;
  return decision;
}

}  // namespace brouk
