#ifndef BROUK_PLATFORM_WIN_PASSWORD_PROMPT_H_
#define BROUK_PLATFORM_WIN_PASSWORD_PROMPT_H_

#include <windows.h>

#include "cef/password_tab.h"
#include "platform/win/theme.h"

namespace brouk::win {

// The password manager's save prompt (008) under the key icon, as on
// macOS: "Save password?" or "Update password?" with the site, the user
// name and password (both editable, the password hidden until the eye is
// pressed), "Save in" for a new Bitwarden login, and Save, Not now and
// Never for this site. Enter saves, Escape or clicking elsewhere is Not
// now (Chrome's bubble). UI thread.
class PasswordPromptBubble {
 public:
  // `done` runs once, when the prompt closes; the owner deletes the prompt
  // afterwards, not from inside `done`.
  PasswordPromptBubble(HWND owner,
                       const PasswordPrompt& prompt,
                       const Theme& theme,
                       HFONT text_font,
                       HFONT icon_font,
                       PasswordPromptDone done);
  ~PasswordPromptBubble();
  PasswordPromptBubble(const PasswordPromptBubble&) = delete;
  PasswordPromptBubble& operator=(const PasswordPromptBubble&) = delete;

  // Shows the prompt below `anchor` (screen pixels), its right edge at the
  // anchor's, with Save focused.
  void ShowBelow(const RECT& anchor);
  HWND hwnd() const { return window_; }

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
  void CreateControls();
  void ToggleReveal();
  // Closes the prompt with `choice`; only the first call counts. Clears the
  // password field.
  void Finish(PasswordPromptAnswer::Choice choice);
  int Scale(int dips) const;

  HWND owner_;
  PasswordPrompt prompt_;
  Theme theme_;
  HFONT text_font_;
  HFONT icon_font_;
  PasswordPromptDone done_;
  HWND window_ = nullptr;
  HWND username_ = nullptr;
  HWND password_ = nullptr;
  HWND reveal_ = nullptr;
  HWND location_ = nullptr;  // Bitwarden: where a new login goes.
  HWND save_ = nullptr;
  HBRUSH background_ = nullptr;
  HBRUSH field_ = nullptr;
  wchar_t password_char_ = 0;  // The edit's own, restored when hidden again.
  bool revealed_ = false;
  bool finished_ = false;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_PASSWORD_PROMPT_H_
