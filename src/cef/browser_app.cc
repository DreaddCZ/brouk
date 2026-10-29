#include "cef/browser_app.h"

#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cef/internal_scheme.h"
#include "cef/profile_prefs.h"
#include "core/page_events.h"
#include "include/cef_command_line.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

void BrowserApp::OnBeforeCommandLineProcessing(
    const CefString& process_type,
    CefRefPtr<CefCommandLine> command_line) {
  // Remote debugging accepts connections from this Mac only (006): a
  // --remote-debugging-address switch cannot open it to the network.
  if (process_type.empty() &&
      command_line->HasSwitch("remote-debugging-address")) {
    command_line->AppendSwitchWithValue("remote-debugging-address",
                                        "127.0.0.1");
  }
#if defined(OS_WIN)
  // Chromium's own profile (behind CEF's global request context) lives in
  // <root>\Default, and the first Brouk profile, "default", in
  // <root>\default: on Windows, whose file names ignore case, the same
  // folder, and the two lock each other's databases ("Profile error
  // occurred"). Chromium's profile gets a folder of its own.
  if (process_type.empty()) {
    command_line->AppendSwitchWithValue("profile-directory", "Chromium");
  }
#endif
  // Developer test runs only: every local rebuild gets a new ad-hoc signature,
  // so macOS asks for the keychain password on each launch. With this
  // variable set, Chromium uses a throwaway key instead of the keychain.
  // Never set for normal use: cookies would not be protected by the keychain.
  if (process_type.empty() && std::getenv("BROUK_DEV_MOCK_KEYCHAIN")) {
    command_line->AppendSwitch("use-mock-keychain");
  }
}

void BrowserApp::OnRegisterCustomPreferences(
    cef_preferences_type_t type,
    CefRawPtr<CefPreferenceRegistrar> registrar) {
  RegisterProfilePreferences(type, registrar);
}

void BrowserApp::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();
  if (callbacks_.on_context_initialized) {
    callbacks_.on_context_initialized();
  }
}

void BrowserApp::OnBeforeChildProcessLaunch(
    CefRefPtr<CefCommandLine> command_line) {
  // Renderer processes start with the current settings they follow; later
  // changes arrive as kRendererConfigMessage.
  if (command_line->GetSwitchValue("type") == "renderer" &&
      callbacks_.json_viewer_enabled) {
    command_line->AppendSwitchWithValue(
        std::string(kRendererJsonViewerSwitch),
        callbacks_.json_viewer_enabled() ? "1" : "0");
  }
}

void BrowserApp::OnRegisterCustomSchemes(
    CefRawPtr<CefSchemeRegistrar> registrar) {
  AddInternalScheme(registrar);
}

// static
LaunchRequest BrowserApp::LaunchRequestFrom(
    const CefRefPtr<CefCommandLine>& command_line) {
  std::vector<CefString> args;
  command_line->GetArguments(args);
  std::vector<std::string> arguments;
  arguments.reserve(args.size());
  for (const CefString& arg : args) {
    arguments.push_back(arg.ToString());
  }
  std::optional<std::string> profile;
  if (command_line->HasSwitch("profile")) {
    profile = command_line->GetSwitchValue("profile").ToString();
  }
  return ParseLaunchRequest(std::move(arguments), std::move(profile));
}

bool BrowserApp::OnAlreadyRunningAppRelaunch(
    CefRefPtr<CefCommandLine> command_line,
    const CefString& /*current_directory*/) {
  CEF_REQUIRE_UI_THREAD();
  if (callbacks_.on_relaunch) {
    callbacks_.on_relaunch(LaunchRequestFrom(command_line));
  }
  return true;
}

}  // namespace brouk
