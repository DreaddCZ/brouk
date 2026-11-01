#ifndef BROUK_PLATFORM_WIN_EXPORT_PASSWORD_DIALOG_H_
#define BROUK_PLATFORM_WIN_EXPORT_PASSWORD_DIALOG_H_

#include <windows.h>

#include <functional>
#include <string>

#include "platform/win/theme.h"

namespace brouk::win {

// What the user chose: a password, going on without the passwords, or
// cancelling the whole export or import.
struct ExportPasswordAnswer {
  enum class Choice { kPassword, kWithoutPasswords, kCancel };
  Choice choice = Choice::kCancel;
  std::string password;
};

// Asks for the password protecting the passwords in a profile export
// (008), as on macOS: a new one, typed twice and at least 8 characters,
// when exporting (`confirm`); the existing one when importing. A small
// window in the middle of `owner`, which is disabled while it is open
// (modal, without a message loop of its own). UI thread.
class ExportPasswordDialog {
 public:
  // `done` runs once, when the dialog closes; the owner deletes the dialog
  // afterwards, not from inside `done`.
  using Done = std::function<void(ExportPasswordAnswer answer)>;

  ExportPasswordDialog(HWND owner,
                       bool confirm,
                       std::wstring message,
                       Done done);
  ~ExportPasswordDialog();
  ExportPasswordDialog(const ExportPasswordDialog&) = delete;
  ExportPasswordDialog& operator=(const ExportPasswordDialog&) = delete;

  void Show();
  // Shows `message` in place of the explanation (a wrong password on
  // import) and lets the user try again.
  void ShowProblem(const std::wstring& message);

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
  // The primary button: checks the passwords (export) and answers.
  void Submit();
  // Closes the dialog with `answer`; only the first call counts.
  void Finish(ExportPasswordAnswer answer);
  int Scale(int dips) const;

  HWND owner_;
  const bool confirm_;
  std::wstring message_;
  Theme theme_;
  Done done_;
  HFONT font_ = nullptr;
  HWND window_ = nullptr;
  HWND text_ = nullptr;
  HWND first_ = nullptr;
  HWND second_ = nullptr;  // Export only: the password again.
  HBRUSH background_ = nullptr;
  HBRUSH field_ = nullptr;
  bool owner_was_enabled_ = false;
  bool finished_ = false;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_EXPORT_PASSWORD_DIALOG_H_
