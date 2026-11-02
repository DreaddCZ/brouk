#ifndef BROUK_PLATFORM_WIN_SIGN_IN_DIALOG_H_
#define BROUK_PLATFORM_WIN_SIGN_IN_DIALOG_H_

#include <windows.h>

#include <optional>
#include <string>

#include "cef/password_tab.h"
#include "platform/win/theme.h"

namespace brouk::win {

// A site's sign-in dialog for HTTP authentication (012, 008), as on macOS:
// the site, user name and password (filled from a saved login), Sign In
// and Cancel. A small window in the middle of `owner`, which is disabled
// while it is open. UI thread.
class SignInDialog {
 public:
  // `done` runs once, when the dialog closes (nullopt: cancelled); the
  // owner deletes the dialog afterwards, not from inside `done`.
  SignInDialog(HWND owner,
               const brouk::HttpAuthDialog& dialog,
               HttpAuthDone done);
  ~SignInDialog();
  SignInDialog(const SignInDialog&) = delete;
  SignInDialog& operator=(const SignInDialog&) = delete;

  void Show();
  // Closes it as Cancel does (the window is closing).
  void Cancel();

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
  // Sign In: answers with the fields' values.
  void Submit();
  // Closes the dialog with `answer`; only the first call counts.
  void Finish(std::optional<HttpAuthAnswer> answer);
  int Scale(int dips) const;

  HWND owner_;
  std::wstring message_;
  std::wstring username_;
  std::wstring password_;
  Theme theme_;
  HttpAuthDone done_;
  HFONT font_ = nullptr;
  HWND window_ = nullptr;
  HWND username_field_ = nullptr;
  HWND password_field_ = nullptr;
  HBRUSH background_ = nullptr;
  HBRUSH field_ = nullptr;
  bool owner_was_enabled_ = false;
  bool finished_ = false;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_SIGN_IN_DIALOG_H_
