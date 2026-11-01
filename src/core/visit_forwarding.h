#ifndef BROUK_CORE_VISIT_FORWARDING_H_
#define BROUK_CORE_VISIT_FORWARDING_H_

#include <cstdint>

namespace brouk {

// How a page came to be shown, for telling whether the page before it
// forwarded here by itself (013): such pages are not kept in the history.
struct VisitArrival {
  // The navigation came from a link or a form (not typed, a bookmark, a
  // reload or back/forward).
  bool link_or_form = false;
  // The user clicked or typed to start it.
  bool user_gesture = false;
  // When the navigation started and when the page before it had finished
  // loading (milliseconds of a steady clock; 0: no page before).
  std::int64_t started_ms = 0;
  std::int64_t previous_loaded_ms = 0;
};

// How soon after loading a page counts as forwarding by itself.
inline constexpr std::int64_t kForwardWindowMs = 5000;

// Whether the previous page forwarded here by itself right after loading:
// a script that submits a form or changes the address without the user
// (sign-in pages that post their answer back, "you are being
// redirected…"). Chromium marks meta refresh and early script navigations
// as client redirects; a form posted from the page's load handler is not
// marked, so this covers it.
bool ForwardedByItself(const VisitArrival& arrival);

}  // namespace brouk

#endif  // BROUK_CORE_VISIT_FORWARDING_H_
