#ifndef BROUK_PLATFORM_WIN_CHOICE_DIALOG_H_
#define BROUK_PLATFORM_WIN_CHOICE_DIALOG_H_

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "platform/win/theme.h"

namespace brouk::win {

// A question with buttons of its own (Windows' message box only has Yes,
// No, OK…): a small window in the middle of `owner`, which is disabled
// while it is open (modal, without a message loop of its own), as the
// alerts on macOS. Buttons are given in reading order and shown left to
// right, the first one is the default (Enter); Escape and the window's
// close button choose `cancel`. UI thread.
class ChoiceDialog {
 public:
  // `done` runs once with the chosen button's index; the owner deletes the
  // dialog afterwards, not from inside `done`.
  using Done = std::function<void(std::size_t choice)>;

  ChoiceDialog(HWND owner,
               std::wstring title,
               std::wstring message,
               std::vector<std::wstring> buttons,
               std::size_t cancel,
               Done done);
  ~ChoiceDialog();
  ChoiceDialog(const ChoiceDialog&) = delete;
  ChoiceDialog& operator=(const ChoiceDialog&) = delete;

  void Show();

 private:
  static LRESULT CALLBACK WindowProc(HWND window,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam);
  static LRESULT CALLBACK ButtonProc(HWND window,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam,
                                     UINT_PTR id,
                                     DWORD_PTR data);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
  // Closes the dialog with `choice`; only the first call counts.
  void Finish(std::size_t choice);
  int Scale(int dips) const;

  HWND owner_;
  std::wstring title_;
  std::wstring message_;
  std::vector<std::wstring> buttons_;
  std::size_t cancel_;
  Theme theme_;
  Done done_;
  HFONT font_ = nullptr;
  HWND window_ = nullptr;
  HWND first_button_ = nullptr;
  HBRUSH background_ = nullptr;
  bool owner_was_enabled_ = false;
  bool finished_ = false;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_CHOICE_DIALOG_H_
