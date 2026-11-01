#include "core/visit_forwarding.h"

namespace brouk {

bool ForwardedByItself(const VisitArrival& arrival) {
  if (!arrival.link_or_form || arrival.user_gesture ||
      arrival.previous_loaded_ms <= 0 ||
      arrival.started_ms < arrival.previous_loaded_ms) {
    return false;
  }
  return arrival.started_ms - arrival.previous_loaded_ms <= kForwardWindowMs;
}

}  // namespace brouk
