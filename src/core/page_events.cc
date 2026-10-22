#include "core/page_events.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "core/external_protocol.h"

namespace brouk {

namespace {

constexpr std::size_t kMaxUrlBytes = 2048;

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
