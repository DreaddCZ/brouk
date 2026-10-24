#ifndef BROUK_CORE_PAGE_EVENTS_H_
#define BROUK_CORE_PAGE_EVENTS_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace brouk {

// Events that the script we add to every page reports to the browser
// process, for things CEF does not report itself (012: blocked popups, tab
// audio) or cannot do itself (the screen sharing picker). The page can call the
// reporting function too, so every event is an untrusted hint: validated here
// and only used to update indicators.

// Name of the process message (renderer -> browser). Arguments: type and
// payload, both strings.
inline constexpr std::string_view kPageEventMessage = "brouk.page_event";

// Event type strings, as sent by the page script.
inline constexpr std::string_view kPageEventPopupBlocked = "popup-blocked";
inline constexpr std::string_view kPageEventAudible = "audible";
inline constexpr std::string_view kPageEventInaudible = "inaudible";
// getDisplayMedia() was called. Payload: "<request id>", plus ":audio" when
// the page also asked for audio.
inline constexpr std::string_view kPageEventDisplayMedia = "display-media";

// Process message (browser -> renderer) with settings the renderer follows
// (a dictionary), sent when they change. New renderer processes get the
// current values as command line switches (kRendererJsonViewerSwitch).
inline constexpr std::string_view kRendererConfigMessage = "brouk.config";
inline constexpr std::string_view kRendererJsonViewerSwitch =
    "brouk-json-viewer";
// Bool: add the JSON viewer to JSON documents (006).
inline constexpr std::string_view kRendererConfigJsonViewer = "json_viewer";

// Cosmetic filtering (007). Renderer -> browser: the frame's resources
// (no arguments; the browser uses the frame's own URL), and generic
// selectors for classes and ids (a JSON string). Browser -> renderer: the
// answers (JSON for the page script, plus the scriptlets to run).
inline constexpr std::string_view kAdblockCosmeticRequest =
    "brouk.adblock.cosmetic";
inline constexpr std::string_view kAdblockCosmeticAnswer =
    "brouk.adblock.cosmetic-answer";
inline constexpr std::string_view kAdblockClassesRequest =
    "brouk.adblock.classes";
inline constexpr std::string_view kAdblockClassesAnswer =
    "brouk.adblock.classes-answer";
// The element picker (007): browser -> renderer starts it in the main frame;
// renderer -> browser carries the picked selector.
inline constexpr std::string_view kAdblockPick = "brouk.adblock.pick";
inline constexpr std::string_view kAdblockPicked = "brouk.adblock.picked";
// The page script's classes request is cut off beyond this.
inline constexpr std::size_t kMaxAdblockClassesBytes = std::size_t{256} * 1024;

struct PageEvent {
  enum class Type { kPopupBlocked, kAudible, kInaudible, kDisplayMedia };
  Type type;
  std::string url;  // kPopupBlocked: the http(s) URL of the blocked popup.
  // kDisplayMedia: the page script's id for the request, echoed in the answer.
  std::uint32_t request_id = 0;
  bool wants_audio = false;  // kDisplayMedia
};

// Parses and validates an event. Returns nullopt for unknown types, bad
// payloads (popup URLs that are not http(s) or longer than 2048 bytes,
// request ids that are not a decimal number below 2^32) or payloads where
// none is expected.
std::optional<PageEvent> ParsePageEvent(std::string_view type,
                                        std::string_view payload);

}  // namespace brouk

#endif  // BROUK_CORE_PAGE_EVENTS_H_
