#ifndef BROUK_PLATFORM_WIN_USER_VERIFICATION_H_
#define BROUK_PLATFORM_WIN_USER_VERIFICATION_H_

#include <windows.h>

#include <functional>
#include <string>

namespace brouk::win {

// Asks the signed-in user to prove it is them before passwords are shown
// or changed (008): Windows Hello (face, fingerprint or PIN) in place of
// Touch ID, or the Windows password when Hello is not set up, as Chrome
// does on Windows. `reason` is shown in the prompt. `done` gets whether it
// succeeded; it runs later, on the UI thread, never before this returns.
// UI thread.
void VerifyUser(HWND owner,
                const std::wstring& reason,
                std::function<void(bool)> done);

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_USER_VERIFICATION_H_
