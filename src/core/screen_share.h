#ifndef BROUK_CORE_SCREEN_SHARE_H_
#define BROUK_CORE_SCREEN_SHARE_H_

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace brouk {

// Screen sharing with a source picker (012, item 3). CEF's media path shares
// the whole main screen unless the page names a source, so the page script
// replaces getDisplayMedia(): it asks the browser, the user picks a screen or
// window, and the script requests exactly that source with getUserMedia().

// Name of the process message (browser -> renderer) answering a
// "display-media" page event. Arguments: the request id (int) and the
// source id (string, empty when the user cancelled).
inline constexpr std::string_view kDisplayMediaChoiceMessage =
    "brouk.display_media_choice";

// A screen or window chosen by the user; `id` is the platform's id (on macOS
// a CGDirectDisplayID or CGWindowID).
struct DesktopSource {
  enum class Type { kScreen, kWindow };
  Type type = Type::kScreen;
  std::uint32_t id = 0;
};

// Chromium's id for the source ("screen:<id>:0", "window:<id>:0"), as
// getUserMedia() expects it in chromeMediaSourceId.
std::string DesktopSourceId(const DesktopSource& source);

// Checks a source id in exactly the form DesktopSourceId() writes; the
// renderer validates the browser's answer with it before the page sees it.
bool IsValidDesktopSourceId(std::string_view id);

// The permission to capture a screen or window, given by choosing it in the
// picker. Valid once, for the site that asked, for a few seconds: the page
// script's getUserMedia() call that follows is allowed without a second
// question. Pages could call getUserMedia() for screen capture themselves,
// so any other such call does not pass through here unasked.
class ScreenShareGrant {
 public:
  using Clock = std::chrono::steady_clock;
  static constexpr std::chrono::seconds kLifetime{10};

  void Issue(std::string origin, Clock::time_point now);
  // True (and the grant is used up) if a grant for `origin` is still valid.
  bool Consume(std::string_view origin, Clock::time_point now);

 private:
  std::string origin_;
  std::optional<Clock::time_point> issued_;
};

}  // namespace brouk

#endif  // BROUK_CORE_SCREEN_SHARE_H_
