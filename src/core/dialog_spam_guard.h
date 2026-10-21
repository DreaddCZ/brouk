#ifndef BROUK_CORE_DIALOG_SPAM_GUARD_H_
#define BROUK_CORE_DIALOG_SPAM_GUARD_H_

namespace brouk {

// Chrome's protection against pages that open dialog after dialog (012):
// from the second JavaScript dialog of a page on, the dialog offers "Prevent
// this page from creating additional dialogs". Once chosen, further dialogs of
// that page are suppressed until it navigates away (Reset).
class DialogSpamGuard {
 public:
  // Whether a new dialog may be shown at all.
  bool IsSuppressed() const { return suppressed_; }
  // Whether the next dialog should offer the "prevent more" checkbox.
  bool ShouldOfferSuppression() const { return shown_count_ > 0; }

  // A dialog of this page was shown and closed; `suppress_more` is the
  // checkbox state.
  void OnDialogClosed(bool suppress_more);

  // The page navigated (CEF: OnResetDialogState).
  void Reset();

 private:
  int shown_count_ = 0;
  bool suppressed_ = false;
};

}  // namespace brouk

#endif  // BROUK_CORE_DIALOG_SPAM_GUARD_H_
