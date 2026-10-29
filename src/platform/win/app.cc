#include "platform/win/app.h"

#include <algorithm>
#include <utility>

#include "cef/browser_app.h"
#include "cef/function_task.h"
#include "core/address_input.h"
#include "core/search_engines.h"
#include "include/base/cef_logging.h"
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"
#include "platform/win/data_paths.h"

namespace brouk::win {

App::App() : profiles_(AppDataDirectory()) {
  profiles_.Load();
  // Web data of profiles deleted earlier; Chromium is not running yet.
  profiles_.RemoveWebDataOfDeletedProfiles(WebDataRootDirectory());
}

void App::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();
  OpenLaunchRequest(
      BrowserApp::LaunchRequestFrom(CefCommandLine::GetGlobalCommandLine()));
}

void App::OnRelaunch(const LaunchRequest& request) {
  CEF_REQUIRE_UI_THREAD();
  OpenLaunchRequest(request);
}

void App::OpenLaunchRequest(const LaunchRequest& request) {
  bool opened = false;
  for (const std::string& url : request.urls) {
    // Untrusted, like text typed into the address bar.
    const AddressInputResult input = ClassifyAddressInput(url);
    if (input.kind != AddressInputResult::Kind::kEmpty) {
      OpenWindow(input.url);
      opened = true;
    }
  }
  if (!opened) {
    OpenWindow(std::string());
  }
}

void App::ReleaseBrowserObjects() {
  windows_.clear();
  context_ = nullptr;
}

CefRefPtr<CefRequestContext> App::RequestContext() {
  CEF_REQUIRE_UI_THREAD();
  if (!context_) {
    CefRequestContextSettings settings;
    CefString(&settings.cache_path) =
        ProfileWebDataDirectory(profiles_.default_profile().id).wstring();
    context_ = CefRequestContext::CreateContext(settings, nullptr);
  }
  return context_;
}

TabClientDelegate::SearchEngine App::SearchEngine() {
  const Settings& global = profiles_.global_settings();
  return {SearchEngineName(global), SearchTemplate(global)};
}

void App::OpenWindow(const std::string& url) {
  CEF_REQUIRE_UI_THREAD();
  auto window = std::make_unique<BrowserWindow>(*this);
  if (!window->Create(url)) {
    LOG(ERROR) << "Could not create a browser window (error "
               << GetLastError() << ")";
    if (windows_.empty()) {
      CefQuitMessageLoop();  // Nothing to show: quit instead of hanging.
    }
    return;
  }
  windows_.push_back(std::move(window));
}

void App::OnWindowDestroyed(BrowserWindow* window) {
  // Deleted from a task: this is called from the window's own procedure.
  CefPostTask(TID_UI, new FunctionTask([this, window] {
                std::erase_if(
                    windows_,
                    [window](const std::unique_ptr<BrowserWindow>& w) {
                      return w.get() == window;
                    });
                // Brouk quits with its last window, as Chrome on Windows.
                if (windows_.empty()) {
                  CefQuitMessageLoop();
                }
              }));
}

}  // namespace brouk::win
