// Entry point of every Brouk process on Windows. CEF's bootstrap.exe, copied
// as brouk.exe, starts the sandbox and calls RunWinMain in brouk.dll (this
// file): the browser process, and the renderer, GPU and other child
// processes, which CefExecuteProcess runs.

#include <windows.h>

#include <shellapi.h>

#include <cwchar>
#include <string>

#include "brouk/app_info.h"
#include "brouk/build_info.h"
#include "cef/browser_app.h"
#include "include/cef_app.h"
#include "include/cef_sandbox_win.h"
#include "platform/win/app.h"
#include "platform/win/data_paths.h"
#include "renderer/renderer_app.h"

namespace {

// "brouk --version" prints "Brouk 0.4.0 (a1b2c3d)": every report points to
// an exact build (019). Brouk is a windows (GUI) program, so this writes to
// the output its parent gave it (a pipe, or the console it was started
// from).
bool PrintVersionIfAsked() {
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  const bool asked =
      argv && argc == 2 && std::wcscmp(argv[1], L"--version") == 0;
  LocalFree(argv);
  if (!asked) {
    return false;
  }
  HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
  if (!output || output == INVALID_HANDLE_VALUE) {
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
      output = GetStdHandle(STD_OUTPUT_HANDLE);
    }
  }
  const std::string line = std::string(brouk::kAppName) + " " +
                           brouk::kAppVersion + " (" + brouk::kAppCommit +
                           ")\n";
  DWORD written = 0;
  WriteFile(output, line.data(), static_cast<DWORD>(line.size()), &written,
            nullptr);
  return true;
}

}  // namespace

CEF_BOOTSTRAP_EXPORT int RunWinMain(HINSTANCE instance,
                                    LPWSTR /*command_line*/,
                                    int /*show*/,
                                    void* sandbox_info,
                                    cef_version_info_t* /*version_info*/) {
  if (PrintVersionIfAsked()) {
    return 0;
  }

  CefMainArgs main_args(instance);
  // Child processes: the same app object serves every type; only renderer
  // processes use its handler. Returns -1 in the browser process.
  CefRefPtr<brouk::RendererApp> renderer_app(new brouk::RendererApp());
  const int exit_code =
      CefExecuteProcess(main_args, renderer_app, sandbox_info);
  if (exit_code >= 0) {
    return exit_code;
  }

  brouk::win::App app;

  CefSettings settings;
  if (!sandbox_info) {
    settings.no_sandbox = true;
  }
  // Chromium's data lives in its own folder (see data_paths.h).
  CefString(&settings.root_cache_path) =
      brouk::win::WebDataRootDirectory().wstring();
#if defined(NDEBUG)
  settings.log_severity = LOGSEVERITY_WARNING;
#endif

  brouk::BrowserApp::Callbacks callbacks;
  callbacks.on_context_initialized = [&app] { app.OnContextInitialized(); };
  callbacks.on_relaunch = [&app](brouk::LaunchRequest request) {
    app.OnRelaunch(request);
  };
  // The setting (006) comes with the settings page (W2).
  callbacks.json_viewer_enabled = [] { return true; };
  CefRefPtr<brouk::BrowserApp> browser_app(new brouk::BrowserApp(callbacks));

  // Returns false also when another instance is already running; that
  // instance gets OnAlreadyRunningAppRelaunch and opens the URLs.
  if (!CefInitialize(main_args, settings, browser_app, sandbox_info)) {
    return CefGetExitCode();
  }
  // Runs until the last window has closed (App::OnWindowDestroyed).
  CefRunMessageLoop();
  app.ReleaseBrowserObjects();
  CefShutdown();
  return 0;
}
