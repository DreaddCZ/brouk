#include "core/dialog_spam_guard.h"

namespace brouk {

void DialogSpamGuard::OnDialogClosed(bool suppress_more) {
  ++shown_count_;
  if (suppress_more) {
    suppressed_ = true;
  }
}

void DialogSpamGuard::Reset() {
  shown_count_ = 0;
  suppressed_ = false;
}

}  // namespace brouk
