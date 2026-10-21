#include "cef/browser_app.h"

#include <cstdlib>

#include "include/cef_command_line.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

void BrowserApp::OnBeforeCommandLineProcessing(
    const CefString& process_type,
    CefRefPtr<CefCommandLine> command_line) {
  // Developer test runs only: every local rebuild gets a new ad-hoc signature,
  // so macOS asks for the keychain password on each launch. With this
  // variable set, Chromium uses a throwaway key instead of the keychain.
  // Never set for normal use: cookies would not be protected by the keychain.
  if (process_type.empty() && std::getenv("BROUK_DEV_MOCK_KEYCHAIN")) {
    command_line->AppendSwitch("use-mock-keychain");
  }
}

void BrowserApp::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();
  if (callbacks_.on_context_initialized) {
    callbacks_.on_context_initialized();
  }
}

bool BrowserApp::OnAlreadyRunningAppRelaunch(
    CefRefPtr<CefCommandLine> /*command_line*/,
    const CefString& /*current_directory*/) {
  CEF_REQUIRE_UI_THREAD();
  if (callbacks_.on_relaunch) {
    callbacks_.on_relaunch();
  }
  return true;
}

}  // namespace brouk
