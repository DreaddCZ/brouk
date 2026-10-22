#include "core/screen_share.h"

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace brouk {

namespace {

// A decimal uint32 without sign or leading zeros (as std::to_string writes).
bool IsCanonicalUint32(std::string_view text) {
  if (text.empty() || text.size() > 10 ||
      text.find_first_not_of("0123456789") != std::string_view::npos ||
      (text.size() > 1 && text.front() == '0')) {
    return false;
  }
  std::uint32_t value = 0;
  auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return error == std::errc() && end == text.data() + text.size();
}

}  // namespace

std::string DesktopSourceId(const DesktopSource& source) {
  std::string prefix =
      source.type == DesktopSource::Type::kScreen ? "screen:" : "window:";
  return prefix + std::to_string(source.id) + ":0";
}

bool IsValidDesktopSourceId(std::string_view id) {
  for (std::string_view prefix : {"screen:", "window:"}) {
    if (id.starts_with(prefix)) {
      id.remove_prefix(prefix.size());
      constexpr std::string_view kSuffix = ":0";
      if (!id.ends_with(kSuffix)) {
        return false;
      }
      id.remove_suffix(kSuffix.size());
      return IsCanonicalUint32(id);
    }
  }
  return false;
}

void ScreenShareGrant::Issue(std::string origin, Clock::time_point now) {
  origin_ = std::move(origin);
  issued_ = now;
}

bool ScreenShareGrant::Consume(std::string_view origin, Clock::time_point now) {
  if (!issued_ || origin.empty() || origin != origin_ || now < *issued_ ||
      now - *issued_ > kLifetime) {
    return false;
  }
  issued_.reset();
  origin_.clear();
  return true;
}

}  // namespace brouk
