#ifndef BROUK_PLATFORM_WIN_AUTOFILL_PROMPT_H_
#define BROUK_PLATFORM_WIN_AUTOFILL_PROMPT_H_

#include <windows.h>

#include <vector>

#include "cef/autofill_tab.h"
#include "platform/win/theme.h"

namespace brouk::win {

// The autofill prompt (009) after a form was submitted, under the address
// bar's star as on macOS: "Add to <label>?", "Update <label>?" with the
// differences, or "Save as new entry?" with a label field. Values failing
// validation are marked. Edit…, Not now, Save as new (on an update) and
// Add / Update / Save; Enter accepts, Escape or clicking elsewhere is Not
// now. UI thread.
class AutofillPromptBubble {
 public:
  // `done` runs once, when the prompt closes; the owner deletes the prompt
  // afterwards, not from inside `done`.
  AutofillPromptBubble(HWND owner,
                       const AutofillPrompt& prompt,
                       const Theme& theme,
                       HFONT font,
                       AutofillPromptDone done);
  ~AutofillPromptBubble();
  AutofillPromptBubble(const AutofillPromptBubble&) = delete;
  AutofillPromptBubble& operator=(const AutofillPromptBubble&) = delete;

  // Shows the prompt below `anchor` (screen pixels), its right edge at the
  // anchor's.
  void ShowBelow(const RECT& anchor);

 private:
  static LRESULT CALLBACK WindowProc(HWND window,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam);
  static LRESULT CALLBACK ControlProc(HWND window,
                                      UINT message,
                                      WPARAM wparam,
                                      LPARAM lparam,
                                      UINT_PTR id,
                                      DWORD_PTR data);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
  int CreateControls(bool measure_only);
  // Closes the prompt with `choice`; only the first call counts.
  void Finish(AutofillPromptAnswer::Choice choice);
  int Scale(int dips) const;

  HWND owner_;
  AutofillPrompt prompt_;
  Theme theme_;
  HFONT font_;
  AutofillPromptDone done_;
  HWND window_ = nullptr;
  HWND label_ = nullptr;  // The new entry's label (Save as new entry).
  HWND accept_ = nullptr;
  std::vector<HWND> problems_;  // Shown in the warning colour.
  HBRUSH background_ = nullptr;
  HBRUSH field_ = nullptr;
  bool finished_ = false;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_AUTOFILL_PROMPT_H_
