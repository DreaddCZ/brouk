#ifndef BROUK_PLATFORM_MAC_BITWARDEN_DIALOGS_H_
#define BROUK_PLATFORM_MAC_BITWARDEN_DIALOGS_H_

#import <AppKit/AppKit.h>

#include <filesystem>
#include <functional>
#include <string>

#include "cef/password_tab.h"

NS_ASSUME_NONNULL_BEGIN

namespace brouk::mac {

// The Bitwarden master password dialog (008), as a sheet on `window` (or a
// window of its own). It stays open while the password is checked, with a
// progress indicator, and shows a failure so the user can try again.
// Main thread.
void AskBitwardenPassword(NSWindow* _Nullable window,
                          const BitwardenPasswordRequest& request,
                          BitwardenPasswordSubmit submit,
                          std::function<void()> cancelled);

// Touch ID only (no fallback to the Mac's password: the fallback is the
// Bitwarden master password). `reason` completes "Brouk wants to …".
// Main thread; `done` is called on the main thread.
void ConfirmWithTouchId(const std::string& reason, TouchIdDone done);

// The certificate authorities that macOS trusts for `server_url`, written
// to a PEM file in `folder` for the Bitwarden CLI (Node.js does not use the
// macOS keychain, so a company's own certificate authority is unknown to
// it). Connects to the server once per run and server; returns "" if the
// server could not be reached or is not trusted. Blocks: never on the main
// thread.
std::filesystem::path PrepareServerCertificates(
    const std::string& server_url,
    const std::filesystem::path& folder);

}  // namespace brouk::mac

NS_ASSUME_NONNULL_END

#endif  // BROUK_PLATFORM_MAC_BITWARDEN_DIALOGS_H_
