#ifndef BROUK_CORE_PAGE_EVENTS_H_
#define BROUK_CORE_PAGE_EVENTS_H_

#include <optional>
#include <string>
#include <string_view>

namespace brouk {

// Events that the script we add to every page reports to the browser
// process, for things CEF does not report itself (012: blocked popups, tab
// audio). The page can call the reporting function too, so every event is an
// untrusted hint: validated here and only used to update indicators.

// Name of the process message (renderer -> browser). Arguments: type and
// payload, both strings.
inline constexpr std::string_view kPageEventMessage = "brouk.page_event";

// Event type strings, as sent by the page script.
inline constexpr std::string_view kPageEventPopupBlocked = "popup-blocked";
inline constexpr std::string_view kPageEventAudible = "audible";
inline constexpr std::string_view kPageEventInaudible = "inaudible";

struct PageEvent {
  enum class Type { kPopupBlocked, kAudible, kInaudible };
  Type type;
  std::string url;  // kPopupBlocked: the http(s) URL of the blocked popup.
};

// Parses and validates an event. Returns nullopt for unknown types, bad
// payloads (popup URLs that are not http(s) or longer than 2048 bytes) or
// payloads where none is expected.
std::optional<PageEvent> ParsePageEvent(std::string_view type,
                                        std::string_view payload);

}  // namespace brouk

#endif  // BROUK_CORE_PAGE_EVENTS_H_
