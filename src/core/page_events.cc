#include "core/page_events.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/external_protocol.h"

namespace brouk {

namespace {

constexpr std::size_t kMaxUrlBytes = 2048;

std::optional<PageEvent> ParseDisplayMedia(std::string_view payload) {
  constexpr std::string_view kAudioSuffix = ":audio";
  bool audio = payload.ends_with(kAudioSuffix);
  if (audio) {
    payload.remove_suffix(kAudioSuffix.size());
  }
  // Plain decimal digits only (from_chars would also accept a leading '-').
  if (payload.empty() || payload.size() > 10 ||
      payload.find_first_not_of("0123456789") != std::string_view::npos) {
    return std::nullopt;
  }
  std::uint32_t id = 0;
  auto [end, error] =
      std::from_chars(payload.data(), payload.data() + payload.size(), id);
  if (error != std::errc() || end != payload.data() + payload.size()) {
    return std::nullopt;
  }
  PageEvent event{PageEvent::Type::kDisplayMedia, {}};
  event.request_id = id;
  event.wants_audio = audio;
  return event;
}

}  // namespace

std::optional<PageEvent> ParsePageEvent(std::string_view type,
                                        std::string_view payload) {
  if (type == kPageEventPopupBlocked) {
    std::string scheme = SchemeOf(payload);
    if (payload.size() > kMaxUrlBytes ||
        (scheme != "http" && scheme != "https")) {
      return std::nullopt;
    }
    return PageEvent{PageEvent::Type::kPopupBlocked, std::string(payload)};
  }
  if (type == kPageEventDisplayMedia) {
    return ParseDisplayMedia(payload);
  }
  if (!payload.empty()) {
    return std::nullopt;
  }
  if (type == kPageEventAudible) {
    return PageEvent{PageEvent::Type::kAudible, {}};
  }
  if (type == kPageEventInaudible) {
    return PageEvent{PageEvent::Type::kInaudible, {}};
  }
  return std::nullopt;
}

}  // namespace brouk
