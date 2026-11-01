#ifndef BROUK_PLATFORM_WIN_BITWARDEN_DIALOG_H_
#define BROUK_PLATFORM_WIN_BITWARDEN_DIALOG_H_

#include <windows.h>

#include <functional>
#include <memory>
#include <string>

#include "cef/password_tab.h"
#include "platform/win/theme.h"

namespace brouk::win {

// The Bitwarden master password dialog (008), as on macOS: "Unlock
// Bitwarden" with the account, or "Confirm it's you" before filling a
// re-prompt item. It stays open while the password is checked ("Unlocking…",
// the controls disabled) and shows a failure so the user can try again. A
// small window in the middle of `owner`, which is disabled while it is
// open. UI thread.
class BitwardenPasswordDialog {
 public:
  // `closed` runs once when the dialog has closed (unlocked or cancelled);
  // the owner deletes the dialog afterwards, not from inside it.
  BitwardenPasswordDialog(HWND owner,
                          const BitwardenPasswordRequest& request,
                          BitwardenPasswordSubmit submit,
                          std::function<void()> cancelled,
                          std::function<void()> closed);
  ~BitwardenPasswordDialog();
  BitwardenPasswordDialog(const BitwardenPasswordDialog&) = delete;
  BitwardenPasswordDialog& operator=(const BitwardenPasswordDialog&) = delete;

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
  void CreateControls(const std::wstring& info, const wchar_t* ok);
  void Submit();
  void SetBusy(bool busy);
  void Failed(const std::string& message);
  // Hides the dialog; `closed` follows.
  void Close();
  int Scale(int dips) const;

  HWND owner_;
  BitwardenPasswordRequest request_;
  BitwardenPasswordSubmit submit_;
  std::function<void()> cancelled_;
  std::function<void()> closed_;
  Theme theme_;
  HWND window_ = nullptr;
  HWND field_ = nullptr;
  HWND problem_ = nullptr;
  HWND progress_ = nullptr;
  HWND ok_ = nullptr;
  HWND cancel_ = nullptr;
  HFONT font_ = nullptr;
  HBRUSH background_ = nullptr;
  HBRUSH field_brush_ = nullptr;
  bool owner_was_enabled_ = false;
  bool busy_ = false;
  bool finished_ = false;
  // Expires with the dialog: the check's answer may come after it closed.
  std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_BITWARDEN_DIALOG_H_
