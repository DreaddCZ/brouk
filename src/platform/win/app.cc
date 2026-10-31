#include "platform/win/app.h"

#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <set>
#include <system_error>
#include <utility>

#include "brouk/app_info.h"
#include "cef/bookmarks_page_handler.h"
#include "cef/browser_app.h"
#include "cef/browsing_data.h"
#include "cef/downloads_page_handler.h"
#include "cef/function_task.h"
#include "cef/help_link.h"
#include "cef/help_page_handler.h"
#include "cef/internal_scheme.h"
#include "cef/profile_manager_handler.h"
#include "cef/static_page_handler.h"
#include "core/address_input.h"
#include "core/bookmark_profile.h"
#include "core/help.h"
#include "core/profile_export.h"
#include "core/search_engines.h"
#include "core/settings_export.h"
#include "core/text_file.h"
#include "core/url_pattern.h"
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
// Bookmark files from other browsers can be large; more is not one.
constexpr std::uintmax_t kMaxBookmarkHtmlBytes = 64 * 1024 * 1024;

std::int64_t NowSeconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

// Puts `text` on the clipboard (the history page's "Copy as CSV").
void SetClipboardText(HWND owner, const std::wstring& text) {
  if (!OpenClipboard(owner)) {
    return;
  }
  EmptyClipboard();
  const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
  if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
    if (void* data = GlobalLock(memory)) {
      std::memcpy(data, text.c_str(), bytes);
      GlobalUnlock(memory);
      // The clipboard owns the memory once it has taken it.
      if (!SetClipboardData(CF_UNICODETEXT, memory)) {
        GlobalFree(memory);
      }
    } else {
      GlobalFree(memory);
    }
  }
  CloseClipboard();
}

// The size of a folder and everything in it, in bytes.
std::int64_t FolderSize(const std::filesystem::path& folder) {
  std::int64_t total = 0;
  std::error_code error;
  for (auto it = std::filesystem::recursive_directory_iterator(
           folder, std::filesystem::directory_options::skip_permission_denied,
           error);
       !error && it != std::filesystem::recursive_directory_iterator();
       it.increment(error)) {
    std::error_code size_error;
    if (it->is_regular_file(size_error)) {
      const std::uintmax_t size = it->file_size(size_error);
      if (!size_error) {
        total += static_cast<std::int64_t>(size);
      }
    }
  }
  return total;
}

// The user's Downloads folder (Explorer's "Downloads"), created if
// missing; empty if Windows has none.
std::string UserDownloadsFolder() {
  PWSTR path = nullptr;
  std::string folder;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_CREATE,
                                     nullptr, &path))) {
    folder = WideToUtf8(path);
  }
  CoTaskMemFree(path);
  return folder;
}

// A folder the downloads setting names, with %VARIABLES% expanded, if it
// exists and is a folder.
std::optional<std::string> UsableFolder(const std::string& configured) {
  const std::wstring wide = Utf8ToWide(configured);
  std::wstring expanded(32768, L'\0');
  const DWORD length = ExpandEnvironmentStringsW(
      wide.c_str(), expanded.data(), static_cast<DWORD>(expanded.size()));
  if (length == 0 || length > expanded.size()) {
    return std::nullopt;
  }
  expanded.resize(length - 1);
  std::error_code error;
  if (!std::filesystem::is_directory(expanded, error)) {
    return std::nullopt;
  }
  return WideToUtf8(expanded);
}

// File actions on a finished download, for the downloads page (014). The
// paths are the downloads' own, as the service recorded them.
bool DownloadedFileExists(const std::string& path) {
  std::error_code error;
  return !path.empty() &&
         std::filesystem::is_regular_file(Utf8ToWide(path), error);
}

void OpenDownloadedFile(const std::string& path) {
  if (DownloadedFileExists(path)) {
    // The file's own app, as a double-click in Explorer opens it (Chrome's
    // "Open"); the path is a download the service recorded, never a
    // command line, and Windows checks files from the internet itself.
    ShellExecuteW(  // nosemgrep: cpp-process-spawn
        nullptr, L"open", Utf8ToWide(path).c_str(), nullptr, nullptr,
        SW_SHOWNORMAL);
  }
}

void RevealDownloadedFile(const std::string& path) {
  if (path.empty()) {
    return;
  }
  // File Explorer with the file selected ("Show in folder").
  std::wstring wide = Utf8ToWide(path);
  std::ranges::replace(wide, L'/', L'\\');
  if (PIDLIST_ABSOLUTE item = ILCreateFromPathW(wide.c_str())) {
    SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
    ILFree(item);
  }
}

// Moves the file to the Recycle Bin (Chrome's "Delete" on Windows).
bool RecycleDownloadedFile(const std::string& path) {
  if (!DownloadedFileExists(path)) {
    return false;
  }
  std::wstring from = Utf8ToWide(path);
  std::ranges::replace(from, L'/', L'\\');
  from.push_back(L'\0');  // A list of paths, ended by an empty one.
  SHFILEOPSTRUCTW operation = {};
  operation.wFunc = FO_DELETE;
  operation.pFrom = from.c_str();
  operation.fFlags =
      FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
  return SHFileOperationW(&operation) == 0 && !operation.fAnyOperationsAborted;
}

// Answers one downloads tab's page (014): the app's downloads, and file
// actions through Windows.
class AppDownloadsPage final : public DownloadsPageDelegate {
 public:
  AppDownloadsPage(App& app, BrowserWindow& window, TabId tab)
      : app_(app), window_(&window), tab_(tab) {}
  ~AppDownloadsPage() override {
    if (watcher_) {
      app_.UnwatchDownloads(watcher_);
    }
  }
  AppDownloadsPage(const AppDownloadsPage&) = delete;
  AppDownloadsPage& operator=(const AppDownloadsPage&) = delete;

  std::vector<ProfileInfo> Profiles() override { return app_.ProfileList(); }
  ProfileId CurrentProfile() override {
    return app_.IsOpenWindow(window_) ? window_->ProfileOfTab(tab_)
                                      : app_.DefaultProfile();
  }
  DownloadService* Service() override { return app_.Downloads(); }
  bool FileExists(const std::string& path) override {
    return DownloadedFileExists(path);
  }
  void OpenFile(const std::string& path) override { OpenDownloadedFile(path); }
  void RevealFile(const std::string& path) override {
    RevealDownloadedFile(path);
  }
  bool TrashFile(const std::string& path) override {
    return RecycleDownloadedFile(path);
  }
  void CopyText(const std::string& text) override {
    SetClipboardText(app_.IsOpenWindow(window_) ? window_->hwnd() : nullptr,
                     Utf8ToWide(text));
  }
  void Retry(const DownloadEntry& entry) override {
    if (app_.IsOpenWindow(window_)) {
      window_->RetryDownload(entry);
    }
  }
  void Watch(std::function<void()> changed) override {
    if (watcher_) {
      app_.UnwatchDownloads(watcher_);
    }
    watcher_ = app_.WatchDownloads(std::move(changed));
  }

 private:
  App& app_;
  // Checked with IsOpenWindow before use: the window may close first.
  BrowserWindow* window_;
  TabId tab_;
  int watcher_ = 0;
};

// Answers one bookmark manager tab's page (011): the app's bookmarks, and
// opening and file dialogs through the window holding the tab.
class AppBookmarksPage final : public BookmarksPageDelegate {
 public:
  AppBookmarksPage(App& app, BrowserWindow& window, TabId tab)
      : app_(app), window_(&window), tab_(tab) {}
  ~AppBookmarksPage() override {
    if (BookmarkStore* store = app_.Bookmarks(); store && watcher_) {
      store->Unwatch(watcher_);
    }
  }
  AppBookmarksPage(const AppBookmarksPage&) = delete;
  AppBookmarksPage& operator=(const AppBookmarksPage&) = delete;

  std::vector<ProfileInfo> Profiles() override { return app_.ProfileList(); }
  ProfileId CurrentProfile() override {
    return app_.IsOpenWindow(window_) ? window_->ProfileOfTab(tab_)
                                      : app_.DefaultProfile();
  }
  // The store goes before the pages at quit: each call looks it up.
  const BookmarkModel& Model() override {
    static const BookmarkModel kEmpty;
    BookmarkStore* store = app_.Bookmarks();
    return store ? store->model() : kEmpty;
  }
  void Change(const std::function<void(BookmarkModel&)>& change) override {
    if (BookmarkStore* store = app_.Bookmarks()) {
      store->Change(change);
    }
  }
  void Remove(const std::vector<BookmarkId>& ids) override {
    if (BookmarkStore* store = app_.Bookmarks()) {
      store->Remove(ids);
    }
  }
  bool Undo() override {
    BookmarkStore* store = app_.Bookmarks();
    if (!store || !store->CanUndoRemove()) {
      return false;
    }
    store->UndoRemove();
    return true;
  }
  void Open(BookmarkId id, bool new_tab) override {
    if (app_.IsOpenWindow(window_)) {
      window_->OpenBookmarkFromTab(tab_, id, new_tab);
    }
  }
  void Favicons(const ProfileId& profile,
                std::vector<std::string> urls,
                IconsDone done) override {
    HistoryService* history = app_.History();
    if (!history) {
      done({});
      return;
    }
    history->Favicons(profile, std::move(urls), std::move(done));
  }
  void Watch(std::function<void()> changed) override {
    BookmarkStore* store = app_.Bookmarks();
    if (!store) {
      return;
    }
    if (watcher_) {
      store->Unwatch(watcher_);
    }
    watcher_ = store->Watch(std::move(changed));
  }
  void SaveHtml(std::string html, SaveDone done) override {
    FileDialogOptions options;
    options.title = L"Export Bookmarks";
    options.type_name = L"HTML bookmark file";
    options.extension = L"html";
    options.file_name = L"Bookmarks";
    HWND owner = app_.IsOpenWindow(window_) ? window_->hwnd() : nullptr;
    // Asked from a task: the dialog runs its own message loop, which must
    // not run inside the page's request.
    CefPostTask(TID_UI, new FunctionTask([options, owner, html, done] {
                  std::optional<std::filesystem::path> path =
                      AskSavePath(owner, options);
                  if (!path) {
                    done(false, std::nullopt);
                    return;
                  }
                  std::optional<Error> error =
                      WriteTextFileAtomically(*path, html);
                  done(!error, error);
                }));
  }
  void OpenHtml(OpenDone done) override {
    FileDialogOptions options;
    options.title = L"Import Bookmarks";
    options.type_name = L"HTML bookmark file";
    options.extension = L"html";
    options.note =
        L"An HTML bookmark file, as exported by Chrome, Edge or Firefox. The "
        L"bookmarks go into \u201CImported\u201D under Other bookmarks.";
    HWND owner = app_.IsOpenWindow(window_) ? window_->hwnd() : nullptr;
    CefPostTask(TID_UI, new FunctionTask([options, owner, done] {
                  std::optional<std::filesystem::path> path =
                      AskOpenPath(owner, options);
                  if (!path) {
                    done(std::nullopt, std::nullopt);
                    return;
                  }
                  Result<std::string> text =
                      ReadTextFile(*path, kMaxBookmarkHtmlBytes);
                  if (!text) {
                    done(std::nullopt, text.error());
                    return;
                  }
                  done(std::move(text.value()), std::nullopt);
                }));
  }

 private:
  App& app_;
  // Checked with IsOpenWindow before use: the window may close first.
  BrowserWindow* window_;
  TabId tab_;
  int watcher_ = 0;
};

// Answers one history tab's page (013). Actions go to the app and the
// window holding the tab, if it is still open.
class AppHistoryPage final : public HistoryPageDelegate {
 public:
  AppHistoryPage(App& app, BrowserWindow& window, TabId tab)
      : app_(app), window_(&window), tab_(tab) {}

  std::vector<ProfileInfo> Profiles() override { return app_.ProfileList(); }
  HistoryService* History() override { return app_.History(); }
  void OpenPage(const ProfileId& profile,
                const std::string& url,
                bool new_tab) override {
    if (app_.IsOpenWindow(window_)) {
      window_->OpenPageFromTab(tab_, profile, url, new_tab);
    } else {
      app_.OpenWindow(url, profile);
    }
  }
  std::string View() override { return app_.HistoryView(); }
  void SetView(const std::string& view) override {
    app_.SetGlobalSetting("history.view", SettingValue(view));
  }
  void CopyText(const std::string& text) override {
    SetClipboardText(app_.IsOpenWindow(window_) ? window_->hwnd() : nullptr,
                     Utf8ToWide(text));
  }
  ProfileId CurrentProfile() override {
    return app_.IsOpenWindow(window_) ? window_->ProfileOfTab(tab_)
                                      : app_.DefaultProfile();
  }
  std::vector<std::string> KeepList(const ProfileId& profile) override {
    return app_.KeepList(profile);
  }
  void SetKeepList(const ProfileId& profile,
                   const std::vector<std::string>& hosts) override {
    std::string joined;
    for (const std::string& host : hosts) {
      joined += joined.empty() ? host : ", " + host;
    }
    app_.SetProfileSetting(profile, "history.keep_list", SettingValue(joined));
  }
  void PreviewProfileData(const ProfileId& profile,
                          std::int64_t since,
                          DataPreviewDone done) override {
    app_.PreviewProfileData(profile, since, std::move(done));
  }
  void ClearProfileData(const ProfileId& profile,
                        std::int64_t since,
                        bool cookies_and_site_data,
                        bool cache,
                        std::set<std::string> sites,
                        std::function<void(bool)> done) override {
    app_.ClearProfileData(profile, since, cookies_and_site_data, cache,
                          std::move(sites), std::move(done));
  }

 private:
  App& app_;
  // Checked with IsOpenWindow before use: the window may close first.
  BrowserWindow* window_;
  TabId tab_;
};

}  // namespace

App::App()
    : profiles_(AppDataDirectory()),
      download_paths_([](const std::string& path) {
        std::error_code error;
        return std::filesystem::exists(Utf8ToWide(path), error);
      }) {
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
  // History (013): one database per profile, as on macOS.
  const std::filesystem::path profiles_dir = AppDataDirectory() / L"profiles";
  history_ = std::make_unique<HistoryService>(
      [profiles_dir](const ProfileId& profile) {
        return profiles_dir / profile / "history.db";
      });
  // Bookmarks (011), one file for the app, as on macOS.
  bookmarks_ =
      std::make_unique<BookmarkStore>(AppDataDirectory() / L"bookmarks.json");
  bookmarks_->Watch([this] { BookmarksChanged(); });
  // Bookmarked pages keep their icons when they leave the history.
  history_->SetKeptIconPages(bookmarks_->model().AllUrls());
  // Downloads (014), recorded in the history.
  downloads_ = std::make_unique<DownloadService>(history_.get());
  downloads_->SetChangedCallback([this] { DownloadsChanged(); });
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

BrowserWindow* App::FrontWindow() const {
  for (HWND hwnd = GetTopWindow(nullptr); hwnd;
       hwnd = GetWindow(hwnd, GW_HWNDNEXT)) {
    for (const std::unique_ptr<BrowserWindow>& window : windows_) {
      if (window->hwnd() == hwnd && window->is_normal() && !window->closing()) {
        return window.get();
      }
    }
  }
  return nullptr;
}

void App::OnTabClosedToReopen(ClosedTab tab) {
  if (!quitting_ && !frozen_session_) {
    closed_tabs_.AddTab(std::move(tab));
  }
}

void App::OnWindowClosedToReopen(std::int64_t window_id,
                                 std::vector<ClosedTab> tabs) {
  // Quitting (the last window, or Exit) adds nothing, as on macOS.
  if (!quitting_ && !frozen_session_) {
    closed_tabs_.AddWindow(window_id, std::move(tabs));
  }
}

const std::deque<ClosedEntry>& App::RecentlyClosed() {
  return closed_tabs_.entries();
}

// A window comes back as a new window with its tabs; a tab goes back into
// its window if that is still open, else into the front window (013).
void App::ReopenClosed(std::optional<std::int64_t> entry_id) {
  CEF_REQUIRE_UI_THREAD();
  std::optional<ClosedEntry> entry =
      entry_id ? closed_tabs_.Take(*entry_id) : closed_tabs_.TakeNewest();
  if (!entry || entry->tabs.empty()) {
    return;
  }
  BrowserWindow* target = nullptr;
  if (!entry->is_window) {
    for (const std::unique_ptr<BrowserWindow>& window : windows_) {
      if (window->window_id() == entry->window_id && window->is_normal() &&
          !window->closing()) {
        target = window.get();
      }
    }
    if (!target) {
      target = FrontWindow();
    }
  }
  bool created = false;
  if (!target) {
    auto window =
        std::make_unique<BrowserWindow>(*this, BrowserWindow::Kind::kNormal);
    if (!window->Create()) {
      LOG(ERROR) << "Could not create a browser window (error "
                 << GetLastError() << ")";
      return;
    }
    target = window.get();
    windows_.push_back(std::move(window));
    frozen_session_.reset();
    created = true;
  }
  for (std::size_t i = 0; i < entry->tabs.size(); ++i) {
    ClosedTab tab = entry->tabs[i];
    if (!profiles_.Find(tab.profile)) {
      tab.profile = profiles_.default_profile().id;  // Deleted meanwhile.
    }
    if (entry->is_window) {
      tab.position = i;
    }
    target->RestoreClosedTab(tab, /*activate=*/i == 0);
  }
  if (created) {
    target->Show();
  } else {
    target->Activate();
  }
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

bool App::OnWindowClosing(BrowserWindow& window) {
  CEF_REQUIRE_UI_THREAD();
  if (!window.is_normal()) {
    return true;
  }
  const bool last = std::ranges::none_of(
      windows_, [&window](const std::unique_ptr<BrowserWindow>& other) {
        return other.get() != &window && other->is_normal() &&
               !other->closing();
      });
  // Exit has asked already (and frozen the session).
  if (last && !frozen_session_ && !ConfirmQuitWithDownloads(window.hwnd())) {
    return false;
  }
  if (!session_ready_ || frozen_session_) {
    return true;
  }
  if (!last) {
    OnSessionChanged();
    return true;
  }
  // Closing the last window quits Brouk (as Chrome on Windows): the next
  // start continues with this window, not with what is left of it once
  // its tabs have closed.
  frozen_session_ = CurrentSession();
  SaveSessionFile(*frozen_session_);
  return true;
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
  bookmarks_.reset();  // Writes pending changes.
  download_watchers_.clear();
  downloads_.reset();  // Before the history it records into.
  history_.reset();    // Finishes queued history work, closes the databases.
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
    const std::string& page,
    BrowserWindow& window,
    TabId tab) {
  // The other pages come with their features (024, W2 and W3).
  if (page == "credits") {
    return std::make_unique<StaticPageHandler>();
  }
  if (page == "bookmarks" && bookmarks_) {
    return std::make_unique<BookmarksPageHandler>(
        std::make_unique<AppBookmarksPage>(*this, window, tab));
  }
  if (page == "downloads" && downloads_) {
    return std::make_unique<DownloadsPageHandler>(
        std::make_unique<AppDownloadsPage>(*this, window, tab));
  }
  if (page == "history" && history_) {
    return std::make_unique<HistoryPageHandler>(
        std::make_unique<AppHistoryPage>(*this, window, tab));
  }
  return nullptr;
}

HistoryService* App::History() {
  return history_.get();
}

void App::OnHistoryChanged(const ProfileId& profile) {
  if (!history_) {
    return;
  }
  // Read in the background; the menu shows the result next time.
  history_->RecentPages(profile, 10,
                        [this, profile](std::vector<HistoryMatch> pages) {
                          recent_pages_[profile] = std::move(pages);
                        });
}

const std::vector<HistoryMatch>* App::RecentPages(const ProfileId& profile) {
  auto it = recent_pages_.find(profile);
  if (it == recent_pages_.end()) {
    OnHistoryChanged(profile);
    return nullptr;
  }
  return &it->second;
}

BookmarkStore* App::Bookmarks() {
  return bookmarks_.get();
}

bool App::BookmarkSuggestionsEnabled() {
  return profiles_.global_settings().GetBool("suggestions.bookmarks");
}

void App::BookmarksChanged() {
  for (const std::unique_ptr<BrowserWindow>& window : windows_) {
    window->BookmarksChanged();
  }
  if (history_ && bookmarks_) {
    history_->SetKeptIconPages(bookmarks_->model().AllUrls());
  }
}

DownloadService* App::Downloads() {
  return downloads_.get();
}

bool App::AskWhereToSaveDownloads() {
  return profiles_.global_settings().GetBool("downloads.ask_where_to_save");
}

// Downloads (014): the profile's own folder if one is set, else the folder
// for all profiles, else the user's Downloads folder. A set folder that is
// missing makes the download ask, starting in Downloads.
DownloadTarget App::ReserveDownloadPath(const std::string& suggested,
                                        const ProfileId& profile) {
  CEF_REQUIRE_UI_THREAD();
  const std::string fallback = UserDownloadsFolder();
  std::optional<Settings> settings = profiles_.ProfileSettings(profile);
  std::string configured =
      settings ? settings->GetString("downloads.folder") : std::string();
  if (configured.empty()) {
    configured = profiles_.global_settings().GetString("downloads.folder");
  }
  if (configured.empty()) {
    return {download_paths_.Reserve(fallback, suggested), fallback.empty()};
  }
  if (std::optional<std::string> folder = UsableFolder(configured)) {
    return {download_paths_.Reserve(*folder, suggested), false};
  }
  return {download_paths_.Reserve(fallback, suggested), true};
}

void App::ReleaseDownloadPath(const std::string& path) {
  download_paths_.Release(path);
}

int App::WatchDownloads(std::function<void()> changed) {
  const int id = next_download_watcher_++;
  download_watchers_[id] = std::move(changed);
  return id;
}

void App::UnwatchDownloads(int id) {
  download_watchers_.erase(id);
}

void App::DownloadsChanged() {
  for (const std::unique_ptr<BrowserWindow>& window : windows_) {
    window->DownloadsChanged();
  }
  // A copy: a page may stop watching while it is told.
  std::vector<std::function<void()>> watchers;
  for (const auto& [id, changed] : download_watchers_) {
    watchers.push_back(changed);
  }
  for (const std::function<void()>& changed : watchers) {
    changed();
  }
}

bool App::ConfirmQuitWithDownloads(HWND owner) {
  const int running = downloads_ ? downloads_->RunningCount() : 0;
  if (running == 0) {
    return true;
  }
  const std::wstring text =
      running == 1 ? L"1 download is in progress.\n\nCancel it and quit?"
                   : std::to_wstring(running) +
                         L" downloads are in progress.\n\nCancel them and "
                         L"quit?";
  return MessageBoxW(owner, text.c_str(), Utf8ToWide(kAppName).c_str(),
                     MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
}

std::string App::SearchSuggestTemplate() {
  return brouk::SearchSuggestTemplate(profiles_.global_settings());
}

bool App::HistorySuggestionsEnabled() {
  return profiles_.global_settings().GetBool("suggestions.history");
}

std::string App::HistoryView() const {
  return profiles_.global_settings().GetString("history.view");
}

std::vector<std::string> App::KeepList(const ProfileId& profile) {
  std::vector<std::string> hosts;
  std::optional<Settings> settings = profiles_.ProfileSettings(profile);
  if (!settings) {
    return hosts;
  }
  for (const UrlPattern& pattern :
       ParseUrlPatternList(settings->GetString("history.keep_list"))
           .value_or(std::vector<UrlPattern>{})) {
    hosts.push_back(pattern.host);
  }
  return hosts;
}

// Cookies are read and deleted with a browser of the profile running: a
// profile's network context (and its cookies) exists only then.
void App::PreviewProfileData(const ProfileId& profile,
                             std::int64_t since,
                             HistoryPageDelegate::DataPreviewDone done) {
  CEF_REQUIRE_UI_THREAD();
  const std::filesystem::path cache =
      ProfileWebDataDirectory(profile) / L"Cache";
  CefRefPtr<CefRequestContext> context = RequestContext(profile);
  auto shared =
      std::make_shared<HistoryPageDelegate::DataPreviewDone>(std::move(done));
  WithBrowserInProfile(profile, [context, since, cache, shared](
                                    CefRefPtr<CefBrowser> /*browser*/,
                                    std::function<void()> release) {
    VisitCookiesSince(
        context, since, /*remove=*/false,
        [cache, shared, release](int cookies, std::set<std::string> /*hosts*/) {
          release();
          // The cache can hold many files: counted on a file thread.
          CefPostTask(TID_FILE_USER_BLOCKING,
                      new FunctionTask([cache, shared, cookies] {
                        const std::int64_t bytes = FolderSize(cache);
                        CefPostTask(TID_UI,
                                    new FunctionTask([shared, cookies, bytes] {
                                      (*shared)(cookies, bytes);
                                    }));
                      }));
        });
  });
}

void App::ClearProfileData(const ProfileId& profile,
                           std::int64_t since,
                           bool cookies,
                           bool cache,
                           std::set<std::string> sites,
                           std::function<void(bool cookies_ok)> done) {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefRequestContext> context = RequestContext(profile);
  auto finish = std::make_shared<std::function<void(bool)>>(std::move(done));
  auto cookies_ok = std::make_shared<bool>(true);
  // The cache last (it does not depend on the rest).
  auto clear_cache = [context, cache, finish, cookies_ok] {
    if (cache) {
      ClearHttpCache(context, [finish, cookies_ok] { (*finish)(*cookies_ok); });
    } else {
      (*finish)(*cookies_ok);
    }
  };
  if (!cookies) {
    clear_cache();
    return;
  }
  auto shared_sites = std::make_shared<std::set<std::string>>(std::move(sites));
  WithBrowserInProfile(profile, [context, since, shared_sites, clear_cache,
                                 cookies_ok](CefRefPtr<CefBrowser> browser,
                                             std::function<void()> release) {
    VisitCookiesSince(context, since, /*remove=*/true,
                      [browser, release, shared_sites, clear_cache, cookies_ok](
                          int count, std::set<std::string> cookie_hosts) {
                        *cookies_ok = count >= 0;
                        shared_sites->insert(cookie_hosts.begin(),
                                             cookie_hosts.end());
                        ClearSiteData(browser, OriginsOfHosts(*shared_sites),
                                      [release, clear_cache] {
                                        release();
                                        clear_cache();
                                      });
                      });
  });
}

void App::WithBrowserInProfile(
    const ProfileId& profile,
    std::function<void(CefRefPtr<CefBrowser> browser,
                       std::function<void()> release)> work) {
  CEF_REQUIRE_UI_THREAD();
  for (const std::unique_ptr<BrowserWindow>& window : windows_) {
    if (CefRefPtr<CefBrowser> browser = window->AnyBrowserInProfile(profile)) {
      work(browser, [] {});
      return;
    }
  }
  // A hidden window holds the helper browser until it has closed.
  HWND hidden =
      CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 200, 200, nullptr,
                      nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!hidden) {
    work(nullptr, [] {});
    return;
  }
  ++helper_browsers_;
  CefRefPtr<CefClient> client = MakeHelperBrowserClient(
      [work](CefRefPtr<CefBrowser> browser) {
        work(browser, [browser] { browser->GetHost()->CloseBrowser(true); });
      },
      [this, hidden] {
        DestroyWindow(hidden);
        --helper_browsers_;
        QuitWhenDone();
      });
  CefWindowInfo info;
  info.SetAsChild(hidden, CefRect(0, 0, 200, 200));
  info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  CefBrowserSettings settings;
  CefBrowserHost::CreateBrowser(info, client, "about:blank", settings, nullptr,
                                RequestContext(profile));
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
  BrowserWindow* front = FrontWindow();
  if (!ConfirmQuitWithDownloads(front ? front->hwnd() : nullptr)) {
    return;
  }
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
  if (quitting_ && helper_browsers_ == 0 &&
      std::ranges::none_of(PageWindows(),
                           [](const auto* page) { return *page != nullptr; })) {
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
  // Its linked bookmarks stay, as general ones (011).
  if (const int linked = bookmarks_ ? bookmarks_->model().CountLinkedTo(id) : 0;
      linked > 0) {
    text += L" " + std::to_wstring(linked) +
            (linked == 1 ? L" bookmark or folder linked to it becomes"
                         : L" bookmarks and folders linked to it become") +
            L" general.";
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
                closed_tabs_.RemoveProfile(id);
                if (bookmarks_) {
                  bookmarks_->Change(
                      [&id](BookmarkModel& model) { model.UnlinkProfile(id); });
                }
                recent_pages_.erase(id);
                if (history_) {
                  history_->CloseProfile(id);  // Before its folder goes.
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
  // Passwords (008) come to Windows later; the file then has the
  // profile's settings and bookmarks.
  const std::string exported_at =
      std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(
                                   std::chrono::system_clock::now()));
  const YamlNode bookmarks =
      bookmarks_ ? ExportProfileBookmarks(bookmarks_->model(), id) : YamlNode();
  std::string text = brouk::ExportProfile(*settings, exported_at, bookmarks);
  FileDialogOptions options;
  options.title = L"Export \u201C" + Utf8ToWide(info->name) + L"\u201D";
  options.type_name = L"Brouk profile";
  options.extension = Utf8ToWide(kProfileExportExtension);
  options.file_name = SafeFileName(Utf8ToWide(info->name));
  options.note =
      L"The file contains the profile's name, colour, settings and the "
      L"bookmarks linked to it. It contains no sign-ins, cookies or cache.";
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
        if (parsed.value().bookmarks && bookmarks_) {
          const ProfileId profile = created.value();
          ProfileBookmarksImport imported;
          bookmarks_->Change([&](BookmarkModel& model) {
            imported = ImportProfileBookmarks(model, *parsed.value().bookmarks,
                                              profile, NowSeconds());
          });
          problems.insert(problems.end(), imported.problems.begin(),
                          imported.problems.end());
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
  if (history_) {
    const std::string& keep = global.GetString("history.keep");
    history_->SetNeverRecord(
        ParseUrlPatternList(global.GetString("history.never_record"))
            .value_or(std::vector<UrlPattern>{}));
    // A choice the settings schema has validated: days, or "forever".
    int days = 0;
    const bool is_days =
        std::from_chars(keep.data(), keep.data() + keep.size(), days).ec ==
        std::errc();
    history_->SetRetentionDays(is_days && days > 0 ? std::optional<int>(days)
                                                   : std::nullopt);
  }
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

// History → Clear browsing data and the settings page: the dialog on the
// history page of the front window (013).
void App::OpenClearBrowsingData() {
  CEF_REQUIRE_UI_THREAD();
  if (quitting_) {
    return;
  }
  BrowserWindow* front = FrontWindow();
  if (!front) {
    OpenWindow(std::string(), ProfileId());
    front = FrontWindow();
  }
  if (front) {
    front->OpenInternalPage("brouk://history/#clear");
    front->Activate();
  }
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
