#include "platform/win/app.h"

#include <algorithm>
#include <filesystem>
#include <utility>

#include "cef/browser_app.h"
#include "cef/function_task.h"
#include "cef/help_link.h"
#include "cef/help_page_handler.h"
#include "cef/internal_scheme.h"
#include "cef/static_page_handler.h"
#include "core/address_input.h"
#include "core/help.h"
#include "core/search_engines.h"
#include "include/base/cef_logging.h"
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"
#include "platform/win/data_paths.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

App::App() : profiles_(AppDataDirectory()) {
  profiles_.Load();
  // Web data of profiles deleted earlier; Chromium is not running yet.
  profiles_.RemoveWebDataOfDeletedProfiles(WebDataRootDirectory());
}

void App::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();
  SetHelpLinkOpeners({
      .open_help = [this](const std::string& anchor) { OpenHelp(anchor); },
      .open_web_link = [this](const std::string& url) { OpenWebLink(url); },
  });
  OpenLaunchRequest(
      BrowserApp::LaunchRequestFrom(CefCommandLine::GetGlobalCommandLine()));
}

void App::OnRelaunch(const LaunchRequest& request) {
  CEF_REQUIRE_UI_THREAD();
  OpenLaunchRequest(request);
}

void App::OpenLaunchRequest(const LaunchRequest& request) {
  ProfileId profile = profiles_.default_profile().id;
  if (request.profile) {
    const ProfileInfo* named = profiles_.FindByName(*request.profile);
    if (!named) {
      named = profiles_.Find(*request.profile);
    }
    if (named) {
      profile = named->id;
    } else {
      LOG(WARNING) << "No profile called \"" << *request.profile
                   << "\"; opening the default profile.";
    }
  }
  std::vector<std::string> urls;
  for (const std::string& url : request.urls) {
    // Untrusted, like text typed into the address bar.
    const AddressInputResult input = ClassifyAddressInput(url);
    if (input.kind != AddressInputResult::Kind::kEmpty) {
      urls.push_back(input.url);
    }
  }
  if (urls.empty()) {
    OpenWindow(std::string(), profile);
    return;
  }
  auto window =
      std::make_unique<BrowserWindow>(*this, BrowserWindow::Kind::kNormal);
  if (!window->Create()) {
    LOG(ERROR) << "Could not create a browser window (error " << GetLastError()
               << ")";
    if (windows_.empty()) {
      CefQuitMessageLoop();  // Nothing to show: quit instead of hanging.
    }
    return;
  }
  for (std::size_t i = 0; i < urls.size(); ++i) {
    window->OpenTab(urls[i], /*foreground=*/i == 0, profile);
  }
  window->Show();
  windows_.push_back(std::move(window));
}

void App::ReleaseBrowserObjects() {
  help_window_.reset();
  windows_.clear();
  contexts_.clear();
  internal_context_ = nullptr;
}

CefRefPtr<CefRequestContext> App::RequestContext(const ProfileId& profile) {
  CEF_REQUIRE_UI_THREAD();
  // Only ids of existing profiles become folder names (they are validated
  // when profiles are read); anything else gets the default profile.
  const ProfileId id =
      profiles_.Find(profile) ? profile : profiles_.default_profile().id;
  auto it = contexts_.find(id);
  if (it != contexts_.end()) {
    return it->second;
  }
  CefRequestContextSettings settings;
  CefString(&settings.cache_path) = ProfileWebDataDirectory(id).wstring();
  CefRefPtr<CefRequestContext> context =
      CefRequestContext::CreateContext(settings, nullptr);
  contexts_[id] = context;
  return context;
}

ProfileId App::DefaultProfile() {
  return profiles_.default_profile().id;
}

TabStrip::GroupLook App::ProfileLook(const ProfileId& profile) {
  if (const ProfileInfo* info = profiles_.Find(profile)) {
    return {info->name, info->color};
  }
  return {profile, "grey"};
}

CefRefPtr<CefRequestContext> App::InternalContext() {
  CEF_REQUIRE_UI_THREAD();
  if (!internal_context_) {
    // A folder of its own, not in memory as on macOS: CEF builds an
    // in-memory context on Chromium's "Default" profile, which is the first
    // Brouk profile's folder on Windows (see BrowserApp's
    // --profile-directory). Internal pages keep nothing of the user's.
    CefRequestContextSettings settings;
    CefString(&settings.cache_path) =
        (WebDataRootDirectory() / L"internal").wstring();
    internal_context_ = CefRequestContext::CreateContext(settings, nullptr);
    // The pages are built next to brouk.exe (CMakeLists.txt).
    wchar_t module[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, module, MAX_PATH);
    const std::filesystem::path web =
        std::filesystem::path(module).parent_path() / L"web";
    InstallInternalPages(internal_context_, WideToUtf8(web.wstring()));
  }
  return internal_context_;
}

std::unique_ptr<CefMessageRouterBrowserSide::Handler> App::InternalPageHandler(
    const std::string& page) {
  // The other pages come with their features (024, W2 and W3).
  if (page == "credits") {
    return std::make_unique<StaticPageHandler>();
  }
  return nullptr;
}

void App::OpenHelp(const std::string& anchor) {
  CEF_REQUIRE_UI_THREAD();
  if (!anchor.empty() && !IsHelpAnchor(anchor)) {
    return;
  }
  if (help_window_) {
    if (!anchor.empty()) {
      // Checked above: only letters, digits and '-'.
      help_window_->ExecuteJavaScript("showHelpSection('" + anchor + "')");
    }
    help_window_->Activate();
    return;
  }
  // One help window (016); deleted once closed.
  auto window = std::make_unique<InternalPageWindow>([this] {
    CefPostTask(TID_UI, new FunctionTask([this] {
                  help_window_.reset();
                  if (quitting_) {
                    CefQuitMessageLoop();
                  }
                }));
  });
  const std::string url =
      anchor.empty() ? "brouk://help/" : "brouk://help/#" + anchor;
  ProfileRegistry* registry = &profiles_;
  if (window->Create(
          url, L"Brouk Help", 980, 720,
          std::make_unique<HelpPageHandler>([registry]() -> const Settings& {
            return registry->global_settings();
          }),
          InternalContext())) {
    help_window_ = std::move(window);
  }
}

void App::OpenWebLink(const std::string& url) {
  if (windows_.empty()) {
    OpenWindow(url, ProfileId());
    return;
  }
  // The window used last: the front one of Brouk's normal windows.
  for (HWND window = GetTopWindow(nullptr); window;
       window = GetWindow(window, GW_HWNDNEXT)) {
    for (const std::unique_ptr<BrowserWindow>& browser_window : windows_) {
      if (browser_window->hwnd() == window && browser_window->is_normal()) {
        browser_window->OpenTab(url, true);
        browser_window->Activate();
        return;
      }
    }
  }
  OpenWindow(url, ProfileId());
}

void App::CloseAllWindows() {
  if (help_window_) {
    PostMessageW(help_window_->hwnd(), WM_CLOSE, 0, 0);
  }
  for (const std::unique_ptr<BrowserWindow>& window : windows_) {
    PostMessageW(window->hwnd(), WM_CLOSE, 0, 0);
  }
}

TabClientDelegate::SearchEngine App::SearchEngine() {
  const Settings& global = profiles_.global_settings();
  return {SearchEngineName(global), SearchTemplate(global)};
}

void App::OpenWindow(const std::string& url, const ProfileId& profile) {
  CEF_REQUIRE_UI_THREAD();
  auto window =
      std::make_unique<BrowserWindow>(*this, BrowserWindow::Kind::kNormal);
  if (!window->Create()) {
    LOG(ERROR) << "Could not create a browser window (error " << GetLastError()
               << ")";
    if (windows_.empty()) {
      CefQuitMessageLoop();  // Nothing to show: quit instead of hanging.
    }
    return;
  }
  window->OpenTab(url, true, profile);
  window->Show();
  windows_.push_back(std::move(window));
}

BrowserWindow* App::OpenPopupWindow(int width, int height) {
  CEF_REQUIRE_UI_THREAD();
  auto window =
      std::make_unique<BrowserWindow>(*this, BrowserWindow::Kind::kPopup);
  if (!window->Create(width, height)) {
    return nullptr;
  }
  window->Show();
  BrowserWindow* popup = window.get();
  windows_.push_back(std::move(window));
  return popup;
}

bool App::IsOpenWindow(const BrowserWindow* window) const {
  return std::ranges::any_of(
      windows_, [window](const auto& w) { return w.get() == window; });
}

void App::OnWindowDestroyed(BrowserWindow* window) {
  // Deleted from a task: this is called from the window's own procedure.
  CefPostTask(TID_UI, new FunctionTask([this, window] {
                std::erase_if(
                    windows_,
                    [window](const std::unique_ptr<BrowserWindow>& w) {
                      return w.get() == window;
                    });
                // Brouk quits with its last window, as Chrome on Windows; an
                // open help window closes first.
                if (windows_.empty() && !quitting_) {
                  quitting_ = true;
                  if (help_window_) {
                    PostMessageW(help_window_->hwnd(), WM_CLOSE, 0, 0);
                  } else {
                    CefQuitMessageLoop();
                  }
                }
              }));
}

}  // namespace brouk::win
