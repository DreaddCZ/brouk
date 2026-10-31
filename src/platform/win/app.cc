#include "platform/win/app.h"

#include <shlobj.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <system_error>
#include <utility>

#include "cef/browser_app.h"
#include "cef/function_task.h"
#include "cef/help_link.h"
#include "cef/help_page_handler.h"
#include "cef/internal_scheme.h"
#include "cef/profile_manager_handler.h"
#include "cef/static_page_handler.h"
#include "core/address_input.h"
#include "core/help.h"
#include "core/profile_export.h"
#include "core/search_engines.h"
#include "core/settings_export.h"
#include "core/text_file.h"
#include "include/base/cef_logging.h"
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"
#include "platform/win/data_paths.h"
#include "platform/win/file_dialogs.h"
#include "platform/win/theme.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

// Profile export files are small (005); bigger files are not ones.
constexpr std::uintmax_t kMaxProfileExportBytes = 16 * 1024 * 1024;
// Settings exports too (as on macOS).
constexpr std::uintmax_t kMaxSettingsExportBytes = 1024 * 1024;

}  // namespace

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
  ApplySettings();
  // Changes made in the files apply while Brouk runs (004).
  settings_watcher_ = std::make_unique<SettingsWatcher>(
      AppDataDirectory(), [this] { SettingsFilesChanged(); });
  const LaunchRequest request =
      BrowserApp::LaunchRequestFrom(CefCommandLine::GetGlobalCommandLine());
  const Session last = StartSession();
  const StartupAction action = DecideStartup(
      profiles_.global_settings().GetString("startup.restore"), last);
  if (action == StartupAction::kRestore) {
    RestoreSession(last);
    if (!windows_.empty()) {
      // Like Chrome, URLs given on the command line open as tabs of the
      // front restored window.
      if (!request.urls.empty()) {
        OpenLaunchRequest(request, windows_.back().get());
      }
      session_ready_ = true;
      return;
    }
  }
  session_ready_ = true;
  OpenLaunchRequest(request);
  if (action == StartupAction::kNewWindowOfferRestore && !windows_.empty()) {
    BrowserWindow* front = windows_.back().get();
    front->ShowRestorePagesBar([this, front, last] {
      // The new tab the start opened is not needed any more (Chrome).
      const bool replace = IsOpenWindow(front) && front->HasOnlyNewTab();
      RestoreSession(last);
      if (replace) {
        PostMessageW(front->hwnd(), WM_CLOSE, 0, 0);
      }
    });
  }
}

void App::OnRelaunch(const LaunchRequest& request) {
  CEF_REQUIRE_UI_THREAD();
  OpenLaunchRequest(request);
}

void App::OpenLaunchRequest(const LaunchRequest& request, BrowserWindow* into) {
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
  const bool profile_given = request.profile.has_value();
  for (const std::string& url : request.urls) {
    // Untrusted, like text typed into the address bar.
    const AddressInputResult input = ClassifyAddressInput(url);
    if (input.kind != AddressInputResult::Kind::kEmpty) {
      urls.push_back(input.url);
    }
  }
  if (urls.empty()) {
    // The profile manager instead of a window, if the user chose so (005).
    if (profiles_.show_manager_at_startup() && !profile_given &&
        windows_.empty() && !profiles_window_) {
      profile_manager_at_startup_ = true;
      OpenProfileManager();
      if (profiles_window_) {
        return;
      }
    }
    OpenWindow(std::string(), profile);
    return;
  }
  if (into && IsOpenWindow(into)) {
    for (std::size_t i = 0; i < urls.size(); ++i) {
      into->OpenTab(urls[i], /*foreground=*/i == 0, profile);
    }
    into->Activate();
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
  frozen_session_.reset();  // A window again: the session goes on.
}

Session App::StartSession() {
  session_path_ = AppDataDirectory() / "session.yaml";
  Result<Session> loaded = LoadSession(session_path_);
  if (!loaded) {
    LOG(WARNING) << "Session: " << loaded.error().message;
  }
  const Session last = loaded ? loaded.value() : Session{};
  Session running = last;
  running.clean_exit = false;
  SaveSessionFile(running);
  return last;
}

void App::RestoreSession(const Session& session) {
  CEF_REQUIRE_UI_THREAD();
  // Saved back to front: the last one restored ends up in front.
  for (const SessionWindow& saved : session.windows) {
    auto window =
        std::make_unique<BrowserWindow>(*this, BrowserWindow::Kind::kNormal);
    if (!window->Create()) {
      LOG(ERROR) << "Could not create a browser window (error "
                 << GetLastError() << ")";
      continue;
    }
    window->Restore(saved);
    window->Show();
    windows_.push_back(std::move(window));
  }
  frozen_session_.reset();
  OnSessionChanged();
}

Session App::CurrentSession() const {
  // Z-order, front to back.
  std::vector<const BrowserWindow*> front_to_back;
  for (HWND hwnd = GetTopWindow(nullptr); hwnd;
       hwnd = GetWindow(hwnd, GW_HWNDNEXT)) {
    for (const std::unique_ptr<BrowserWindow>& window : windows_) {
      if (window->hwnd() == hwnd) {
        front_to_back.push_back(window.get());
        break;
      }
    }
  }
  Session session;
  session.clean_exit = false;
  for (auto it = front_to_back.rbegin(); it != front_to_back.rend(); ++it) {
    if (std::optional<SessionWindow> saved = (*it)->SessionState()) {
      session.windows.push_back(std::move(*saved));
    }
  }
  return session;
}

void App::SaveSessionFile(const Session& session) {
  if (session_path_.empty()) {
    return;
  }
  if (std::optional<Error> error = SaveSession(session_path_, session)) {
    LOG(WARNING) << "Session: " << error->message;
  }
}

void App::OnSessionChanged() {
  CEF_REQUIRE_UI_THREAD();
  if (!session_ready_ || frozen_session_ || session_save_scheduled_) {
    return;
  }
  session_save_scheduled_ = true;
  CefPostDelayedTask(TID_UI, new FunctionTask([this] {
                       session_save_scheduled_ = false;
                       if (session_ready_ && !frozen_session_) {
                         SaveSessionFile(CurrentSession());
                       }
                     }),
                     1500);
}

void App::OnWindowClosing(BrowserWindow& window) {
  CEF_REQUIRE_UI_THREAD();
  if (!session_ready_ || frozen_session_ || !window.is_normal()) {
    return;
  }
  const bool last = std::ranges::none_of(
      windows_, [&window](const std::unique_ptr<BrowserWindow>& other) {
        return other.get() != &window && other->is_normal() &&
               !other->closing();
      });
  if (!last) {
    OnSessionChanged();
    return;
  }
  // Closing the last window quits Brouk (as Chrome on Windows): the next
  // start continues with this window, not with what is left of it once
  // its tabs have closed.
  frozen_session_ = CurrentSession();
  SaveSessionFile(*frozen_session_);
}

void App::SaveSessionAtQuit() {
  if (!session_ready_) {
    return;
  }
  Session session = frozen_session_ ? *frozen_session_ : CurrentSession();
  session.clean_exit = true;
  SaveSessionFile(session);
  frozen_session_ = std::move(session);
  session_ready_ = false;
}

void App::ReleaseBrowserObjects() {
  session_ready_ = false;
  settings_watcher_.reset();
  help_window_.reset();
  profiles_window_.reset();
  settings_window_.reset();
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
  context->SetChromeColorScheme(color_variant_, 0);
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
    internal_context_->SetChromeColorScheme(color_variant_, 0);
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
                  QuitWhenDone();
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
  // Exit: the windows open now are the session to continue with (015).
  if (session_ready_ && !frozen_session_) {
    frozen_session_ = CurrentSession();
    SaveSessionFile(*frozen_session_);
  }
  for (const auto* page : PageWindows()) {
    if (*page) {
      PostMessageW((*page)->hwnd(), WM_CLOSE, 0, 0);
    }
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
  frozen_session_.reset();  // A window again: the session goes on.
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
                // Brouk quits with its last window, as Chrome on Windows;
                // open help and profile manager windows close first.
                if (windows_.empty() && !quitting_) {
                  quitting_ = true;
                  for (const auto* page : PageWindows()) {
                    if (*page) {
                      PostMessageW((*page)->hwnd(), WM_CLOSE, 0, 0);
                    }
                  }
                  QuitWhenDone();
                }
              }));
}

void App::QuitWhenDone() {
  if (quitting_ && std::ranges::none_of(PageWindows(), [](const auto* page) {
        return *page != nullptr;
      })) {
    SaveSessionAtQuit();
    CefQuitMessageLoop();
  }
}

const std::vector<ProfileInfo>& App::ProfileList() {
  return profiles_.profiles();
}

void App::OpenProfileManager() {
  CEF_REQUIRE_UI_THREAD();
  if (quitting_) {
    return;
  }
  if (profiles_window_) {
    profiles_window_->Activate();
    return;
  }
  auto window = std::make_unique<InternalPageWindow>([this] {
    CefPostTask(TID_UI, new FunctionTask([this] {
                  profiles_window_.reset();
                  // Closed at startup without choosing a profile: nothing
                  // else is open, so Brouk quits (Chrome's profile picker).
                  if (windows_.empty() && !quitting_) {
                    quitting_ = true;
                    for (const auto* page : PageWindows()) {
                      if (*page) {
                        PostMessageW((*page)->hwnd(), WM_CLOSE, 0, 0);
                      }
                    }
                  }
                  QuitWhenDone();
                }));
  });
  if (window->Create("brouk://profiles/", L"Profiles", 640, 600,
                     std::make_unique<ProfileManagerHandler>(this),
                     InternalContext())) {
    profiles_window_ = std::move(window);
  }
}

void App::ProfilesChanged() {
  for (const std::unique_ptr<BrowserWindow>& window : windows_) {
    window->ProfilesChanged();
  }
}

std::vector<ProfileManagerDelegate::Summary> App::Profiles() {
  std::vector<Summary> summaries;
  const ProfileId& default_id = profiles_.default_profile().id;
  for (const ProfileInfo& profile : profiles_.profiles()) {
    int tabs = 0;
    for (const std::unique_ptr<BrowserWindow>& window : windows_) {
      tabs += window->TabCountInProfile(profile.id);
    }
    summaries.push_back({profile, profile.id == default_id, tabs});
  }
  return summaries;
}

bool App::ShowAtStartup() {
  return profiles_.show_manager_at_startup();
}

Result<ProfileId> App::CreateProfile(const std::string& name,
                                     const std::string& color) {
  Result<ProfileId> created = profiles_.CreateProfile(name, color);
  ProfilesChanged();
  return created;
}

std::optional<Error> App::UpdateProfile(const ProfileId& id,
                                        const std::string& name,
                                        const std::string& color) {
  std::optional<Error> error = profiles_.UpdateProfile(id, name, color);
  ProfilesChanged();
  return error;
}

std::optional<Error> App::SetDefaultProfile(const ProfileId& id) {
  std::optional<Error> error = profiles_.SetDefaultProfile(id);
  ProfilesChanged();
  return error;
}

std::optional<Error> App::SetShowAtStartup(bool show) {
  return profiles_.SetShowManagerAtStartup(show);
}

std::optional<Error> App::OpenProfile(const ProfileId& id) {
  if (!profiles_.Find(id)) {
    return Error{"There is no such profile."};
  }
  if (quitting_) {
    return Error{"Brouk is closing."};
  }
  OpenWindow(std::string(), id);
  // Chrome's profile picker closes once a profile is chosen at startup.
  if (profile_manager_at_startup_ && profiles_window_) {
    profile_manager_at_startup_ = false;
    PostMessageW(profiles_window_->hwnd(), WM_CLOSE, 0, 0);
  }
  return std::nullopt;
}

void App::DeleteProfile(const ProfileId& id, DeleteDone done) {
  const ProfileInfo* info = profiles_.Find(id);
  if (!info) {
    done(false, Error{"There is no such profile."});
    return;
  }
  if (profiles_.default_profile().id == id) {
    done(false, Error{"The default profile cannot be deleted. Make another "
                      "profile the default first."});
    return;
  }
  int tabs = 0;
  for (const std::unique_ptr<BrowserWindow>& window : windows_) {
    tabs += window->TabCountInProfile(id);
  }
  std::wstring text =
      L"Its sign-ins, cookies, history and settings are removed from this "
      L"PC. This cannot be undone.";
  if (tabs > 0) {
    text += L" Its " + std::to_wstring(tabs) + L" open " +
            (tabs == 1 ? L"tab" : L"tabs") + L" will close.";
  }
  const std::wstring title =
      L"Delete the profile \u201C" + Utf8ToWide(info->name) + L"\u201D?";
  // Asked from a task: the message box runs its own message loop, which
  // must not run inside the page's request.
  HWND owner = DialogOwner();
  CefPostTask(TID_UI, new FunctionTask([this, id, owner, title, text, done] {
                const int answer = MessageBoxW(
                    owner, (title + L"\n\n" + text).c_str(), L"Delete profile",
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
                if (answer != IDYES || quitting_) {
                  done(false, std::nullopt);
                  return;
                }
                for (const std::unique_ptr<BrowserWindow>& window : windows_) {
                  window->CloseTabsInProfile(id);
                }
                contexts_.erase(id);
                // The web data goes at the next start, when Chromium no
                // longer uses it.
                std::optional<Error> error = profiles_.DeleteProfile(id);
                ProfilesChanged();
                done(!error, error);
              }));
}

void App::ExportProfile(const ProfileId& id, ExportDone done) {
  std::optional<Settings> settings = profiles_.ProfileSettings(id);
  const ProfileInfo* info = profiles_.Find(id);
  if (!settings || !info) {
    done(false, Error{"There is no such profile."});
    return;
  }
  // Bookmarks (011) and passwords (008) come to Windows later; the file
  // then has the profile's settings only.
  const std::string exported_at =
      std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(
                                   std::chrono::system_clock::now()));
  std::string text = brouk::ExportProfile(*settings, exported_at);
  FileDialogOptions options;
  options.title = L"Export \u201C" + Utf8ToWide(info->name) + L"\u201D";
  options.type_name = L"Brouk profile";
  options.extension = Utf8ToWide(kProfileExportExtension);
  options.file_name = SafeFileName(Utf8ToWide(info->name));
  options.note =
      L"The file contains the profile's name, colour and settings. It "
      L"contains no sign-ins, cookies or cache.";
  // Asked from a task: the dialog runs its own message loop, which must not
  // run inside the page's request.
  HWND owner = DialogOwner();
  CefPostTask(
      TID_UI, new FunctionTask([options, owner, text, done] {
        std::optional<std::filesystem::path> path = AskSavePath(owner, options);
        if (!path) {
          done(false, std::nullopt);
          return;
        }
        std::optional<Error> error = WriteTextFileAtomically(*path, text);
        done(!error, error);
      }));
}

void App::ImportProfile(ImportDone done) {
  FileDialogOptions options;
  options.title = L"Import a Profile";
  options.type_name = L"Brouk profile";
  options.extension = Utf8ToWide(kProfileExportExtension);
  HWND owner = DialogOwner();
  // From a task, as ExportProfile.
  CefPostTask(
      TID_UI, new FunctionTask([this, options, owner, done] {
        std::optional<std::filesystem::path> path = AskOpenPath(owner, options);
        if (!path || quitting_) {
          done(ImportResult{});
          return;
        }
        // Export files are small; anything big is not one (the
        // parser has a limit too).
        Result<std::string> text = ReadTextFile(*path, kMaxProfileExportBytes);
        if (!text) {
          done(Error{"This file is not a Brouk profile export."});
          return;
        }
        Result<ImportedProfile> parsed = ParseProfileExport(text.value());
        if (!parsed) {
          done(parsed.error());
          return;
        }
        Result<ProfileId> created =
            profiles_.ImportProfile(parsed.value().settings);
        if (!created) {
          done(created.error());
          return;
        }
        std::vector<std::string> problems = parsed.value().problems;
        if (parsed.value().bookmarks) {
          problems.emplace_back(
              "The bookmarks were not imported: Brouk on Windows "
              "has no bookmarks yet.");
        }
        if (parsed.value().passwords) {
          problems.emplace_back(
              "The passwords were not imported: Brouk on Windows "
              "has no password manager yet.");
        }
        ProfilesChanged();
        done(ImportResult{created.value(), problems});
      }));
}

const LinkRoutingConfig* App::LinkRouting() {
  return nullptr;  // Link routing (010) comes with W3.
}

std::optional<Error> App::SetLinkRouting(LinkRoutingConfig /*config*/) {
  return Error{"Link routing comes later on Windows."};
}

std::array<std::unique_ptr<InternalPageWindow>*, 3> App::PageWindows() {
  return {&help_window_, &profiles_window_, &settings_window_};
}

HWND App::DialogOwner() const {
  // The page that asked is in front; the profile manager is the older one.
  for (const auto* page : {&settings_window_, &profiles_window_}) {
    if (*page && GetForegroundWindow() == (*page)->hwnd()) {
      return (*page)->hwnd();
    }
  }
  if (profiles_window_) {
    return profiles_window_->hwnd();
  }
  return settings_window_ ? settings_window_->hwnd() : nullptr;
}

bool App::JsonViewerEnabled() const {
  return profiles_.global_settings().GetBool("content.json_viewer");
}

void App::SettingsFilesChanged() {
  CEF_REQUIRE_UI_THREAD();
  if (quitting_) {
    return;
  }
  profiles_.Load();
  for (const std::string& problem : profiles_.problems()) {
    LOG(WARNING) << "Settings: " << problem;
  }
  ApplySettings();
  ProfilesChanged();
}

void App::ApplySettings() {
  CEF_REQUIRE_UI_THREAD();
  const Settings& global = profiles_.global_settings();
  const std::string& theme = global.GetString("appearance.theme");
  // A choice the settings schema has validated.
  const ThemeChoice choice = theme == "light"  ? ThemeChoice::kLight
                             : theme == "dark" ? ThemeChoice::kDark
                                               : ThemeChoice::kSystem;
  SetThemeChoice(choice);
  // Web pages' prefers-color-scheme, and Chromium's own pages.
  color_variant_ = choice == ThemeChoice::kLight  ? CEF_COLOR_VARIANT_LIGHT
                   : choice == ThemeChoice::kDark ? CEF_COLOR_VARIANT_DARK
                                                  : CEF_COLOR_VARIANT_SYSTEM;
  for (const auto& [id, context] : contexts_) {
    context->SetChromeColorScheme(color_variant_, 0);
  }
  if (internal_context_) {
    internal_context_->SetChromeColorScheme(color_variant_, 0);
  }
  const bool json_viewer = JsonViewerEnabled();
  for (const std::unique_ptr<BrowserWindow>& window : windows_) {
    window->RefreshTheme();
    window->SendRendererConfig(json_viewer);
  }
  for (const auto* page : PageWindows()) {
    if (*page) {
      (*page)->RefreshTheme();
    }
  }
}

void App::OpenSettings(const std::string& url) {
  CEF_REQUIRE_UI_THREAD();
  if (quitting_) {
    return;
  }
  // Only brouk://settings addresses (a fragment names a setting).
  const std::string page =
      url.starts_with("brouk://settings/") ? url : "brouk://settings/";
  if (settings_window_) {
    if (!url.empty()) {
      settings_window_->LoadUrl(page);
    }
    settings_window_->Activate();
    return;
  }
  // One settings window, as on macOS; deleted once closed.
  auto window = std::make_unique<InternalPageWindow>([this] {
    CefPostTask(TID_UI, new FunctionTask([this] {
                  settings_window_.reset();
                  QuitWhenDone();
                }));
  });
  if (window->Create(page, L"Settings", 980, 720,
                     std::make_unique<SettingsHandler>(this),
                     InternalContext())) {
    settings_window_ = std::move(window);
  }
}

const ProfileRegistry& App::Registry() {
  return profiles_;
}

std::optional<Settings> App::ProfileSettings(std::string_view id) {
  return profiles_.ProfileSettings(id);
}

std::optional<Error> App::SetGlobalSetting(std::string_view key,
                                           SettingValue value) {
  std::optional<Error> error =
      profiles_.SetGlobalSetting(key, std::move(value));
  ApplySettings();
  ProfilesChanged();
  return error;
}

std::optional<Error> App::SetProfileSetting(std::string_view id,
                                            std::string_view key,
                                            SettingValue value) {
  std::optional<Error> error =
      profiles_.SetProfileSetting(id, key, std::move(value));
  ProfilesChanged();
  return error;
}

void App::RevealSettingsFolder() {
  const std::filesystem::path file = AppDataDirectory() / L"settings.yaml";
  std::error_code error;
  if (!std::filesystem::exists(file, error)) {
    // Write the defaults, so there is a file to edit.
    profiles_.ReplaceGlobalSettings(profiles_.global_settings());
  }
  // File Explorer with the file selected. Our own path, not a program.
  PIDLIST_ABSOLUTE item = ILCreateFromPathW(file.c_str());
  if (item) {
    SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
    ILFree(item);
  }
}

void App::OpenClearBrowsingData() {
  // Clearing browsing data (013) comes with history (W2).
}

AdblockService* App::Adblock() {
  return nullptr;  // Ad blocking (007) comes later on Windows.
}

std::string App::Language() {
  std::array<wchar_t, LOCALE_NAME_MAX_LENGTH> name{};
  if (GetUserDefaultLocaleName(name.data(), static_cast<int>(name.size())) ==
      0) {
    return "en";
  }
  return WideToUtf8(name.data());
}

AutofillManager* App::Autofill() {
  return nullptr;  // Form autofill (009) comes later on Windows.
}

void App::SaveAutofillFile(std::string /*json*/, TextSaveDone done) {
  done(false, Error{"Autofill comes later on Windows."});
}

void App::OpenAutofillFile(TextOpenDone done) {
  done(std::nullopt, Error{"Autofill comes later on Windows."});
}

void App::ExportSettings(FileDone done) {
  const std::string exported_at =
      std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(
                                   std::chrono::system_clock::now()));
  std::string text =
      brouk::ExportSettings(profiles_.global_settings(), exported_at);
  FileDialogOptions options;
  options.title = L"Export Settings";
  options.type_name = L"Brouk settings";
  options.extension = Utf8ToWide(kSettingsExportExtension);
  options.file_name = L"Brouk settings";
  options.note =
      L"Settings for all profiles. Profiles are exported in the profile "
      L"manager.";
  HWND owner = DialogOwner();
  // From a task: the dialog runs its own message loop, which must not run
  // inside the page's request.
  CefPostTask(
      TID_UI, new FunctionTask([options, owner, text, done] {
        std::optional<std::filesystem::path> path = AskSavePath(owner, options);
        if (!path) {
          done(false, {}, std::nullopt);
          return;
        }
        std::optional<Error> error = WriteTextFileAtomically(*path, text);
        done(!error, {}, error);
      }));
}

void App::ImportSettings(FileDone done) {
  FileDialogOptions options;
  options.title = L"Import Settings";
  options.type_name = L"Brouk settings";
  options.extension = Utf8ToWide(kSettingsExportExtension);
  HWND owner = DialogOwner();
  // From a task, as ExportSettings.
  CefPostTask(
      TID_UI, new FunctionTask([this, options, owner, done] {
        std::optional<std::filesystem::path> path = AskOpenPath(owner, options);
        if (!path || quitting_) {
          done(false, {}, std::nullopt);
          return;
        }
        Result<std::string> text = ReadTextFile(*path, kMaxSettingsExportBytes);
        if (!text) {
          done(false, {}, Error{"This file is not a Brouk settings export."});
          return;
        }
        Result<ImportedSettings> parsed = ParseSettingsExport(text.value());
        if (!parsed) {
          done(false, {}, parsed.error());
          return;
        }
        if (std::optional<Error> error =
                profiles_.ReplaceGlobalSettings(parsed.value().settings)) {
          done(false, {}, error);
          return;
        }
        ApplySettings();
        ProfilesChanged();
        done(true, parsed.value().problems, std::nullopt);
      }));
}

std::vector<std::string> App::LinkRoutingProblems() {
  return {};
}

bool App::IsDefaultBrowser() {
  return false;
}

void App::MakeDefaultBrowser(
    std::function<void(std::optional<Error> error)> done) {
  done(Error{"Making Brouk the default browser comes later on Windows."});
}

}  // namespace brouk::win
