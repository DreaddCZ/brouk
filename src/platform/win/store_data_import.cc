#include "platform/win/store_data_import.h"

#include <windows.h>

#include <commctrl.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <thread>

#include "brouk/app_info.h"
#include "core/log.h"
#include "platform/win/credential_store.h"
#include "platform/win/data_paths.h"
#include "platform/win/store_package.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

// In the package's LocalState, next to (not in) the data folder, which the
// import replaces.
constexpr wchar_t kRequestFile[] = L"import-direct-download";
// In the Store app's data folder.
constexpr wchar_t kOfferedFile[] = L"direct-download-import-offered";

std::filesystem::path RequestPath() {
  const std::filesystem::path state = StoreAppStateDirectory();
  return state.empty() ? std::filesystem::path() : state / kRequestFile;
}

std::wstring AppTitle() {
  return Utf8ToWide(kAppName);
}

bool MutexExists(const std::wstring& name) {
  HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, name.c_str());
  if (!mutex) {
    return false;
  }
  CloseHandle(mutex);
  return true;
}

// What the worker thread hands back.
struct ImportOutcome {
  std::atomic<bool> finished = false;
  std::optional<Error> error;
  // Move only: the old folder could not be deleted after a good copy.
  bool old_folder_kept = false;
};

void Import(DataImportMode mode, ImportOutcome& outcome) {
  const std::filesystem::path from = DirectDownloadDataDirectory();
  const std::filesystem::path to = AppDataDirectory();
  outcome.error = ReplaceDataFolder(from, to);
  if (!outcome.error) {
    const bool move = mode == DataImportMode::kMove;
    std::shared_ptr<SecretStore> source =
        CreateCredentialStore(DirectDownloadCredentialPrefix());
    std::shared_ptr<SecretStore> target =
        CreateCredentialStore(CredentialPrefix());
    outcome.error = CopyDataKeys(from, *source, *target, move);
    if (!outcome.error && move) {
      std::error_code error;
      std::filesystem::remove_all(from, error);
      outcome.old_folder_kept = static_cast<bool>(error);
    }
  }
  if (!outcome.error) {
    SetDirectDownloadImportOffered();
  }
  outcome.finished = true;
}

// A small window saying what happens while the copy runs on its own
// thread; Windows' messages keep being handled, so Brouk does not look
// hung. Nothing to click in it.
void RunWithProgressWindow(DataImportMode mode, ImportOutcome& outcome) {
  constexpr wchar_t kClass[] = L"BroukImportProgress";
  WNDCLASSEXW window_class = {};
  window_class.cbSize = sizeof(window_class);
  window_class.lpfnWndProc = DefWindowProcW;
  window_class.hInstance = GetModuleHandleW(nullptr);
  window_class.hCursor = LoadCursorW(nullptr, IDC_WAIT);
  window_class.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
  window_class.lpszClassName = kClass;
  RegisterClassExW(&window_class);
  RECT area = {};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
  const UINT dpi = GetDpiForSystem();
  const int width = MulDiv(440, static_cast<int>(dpi), 96);
  const int height = MulDiv(110, static_cast<int>(dpi), 96);
  HFONT font = nullptr;
  HWND window = CreateWindowExW(
      WS_EX_DLGMODALFRAME, kClass, AppTitle().c_str(),
      WS_POPUP | WS_CAPTION | WS_VISIBLE, (area.left + area.right - width) / 2,
      (area.top + area.bottom - height) / 2, width, height, nullptr, nullptr,
      GetModuleHandleW(nullptr), nullptr);
  if (window) {
    RECT client = {};
    GetClientRect(window, &client);
    const std::wstring text =
        L"Bringing in the data from the direct "
        L"download of " +
        AppTitle() + L"\u2026";
    HWND label = CreateWindowExW(
        0, WC_STATICW, text.c_str(),
        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE | SS_NOPREFIX, 0, 0,
        client.right, client.bottom, window, nullptr, GetModuleHandleW(nullptr),
        nullptr);
    NONCLIENTMETRICSW metrics = {};
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics),
                               &metrics, 0, dpi);
    font = CreateFontIndirectW(&metrics.lfMessageFont);
    SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  }
  std::thread worker([mode, &outcome] { Import(mode, outcome); });
  while (!outcome.finished) {
    MsgWaitForMultipleObjects(0, nullptr, FALSE, 100, QS_ALLINPUT);
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
  worker.join();
  if (window) {
    DestroyWindow(window);
  }
  if (font) {
    DeleteObject(font);
  }
}

}  // namespace

std::wstring RunningMutexName() {
  return AppTitle() + (IsStoreApp() ? L"StoreRunning" : L"Running");
}

bool CanImportDirectDownloadData() {
  std::error_code error;
  return IsStoreApp() &&
         std::filesystem::is_directory(DirectDownloadDataDirectory(), error);
}

bool DirectDownloadImportOffered() {
  std::error_code error;
  return std::filesystem::exists(AppDataDirectory() / kOfferedFile, error);
}

void SetDirectDownloadImportOffered() {
  // An empty file; if it cannot be written, the question comes again.
  std::ofstream(AppDataDirectory() / kOfferedFile).close();
}

bool RequestDirectDownloadImport(DataImportMode mode) {
  const std::filesystem::path path = RequestPath();
  if (path.empty()) {
    return false;
  }
  std::ofstream file(path, std::ios::trunc);
  file << DataImportRequestText(mode);
  file.close();
  return !file.fail();
}

void RunRequestedDirectDownloadImport() {
  if (!IsStoreApp()) {
    return;
  }
  const std::filesystem::path path = RequestPath();
  std::error_code error;
  if (path.empty() || !std::filesystem::exists(path, error)) {
    return;
  }
  // Another start of the Store app is still running: it has the data open.
  // The request stays for a start after it.
  if (MutexExists(RunningMutexName())) {
    return;
  }
  std::optional<DataImportMode> mode;
  {
    std::ifstream file(path);
    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    mode = ParseDataImportRequest(text);
  }
  // Asked once: whatever happens now, the next start does not import.
  std::filesystem::remove(path, error);
  if (!mode || !CanImportDirectDownloadData()) {
    return;
  }
  const std::wstring title = AppTitle();
  // The direct download must not write its data while it is copied.
  const std::wstring direct_running =
      Utf8ToWide(kAppName) + L"Running";  // Its RunningMutexName().
  while (MutexExists(direct_running)) {
    if (MessageBoxW(nullptr,
                    (title + L" from the direct download is running. Quit it "
                             L"(⋮ → Exit), then choose Retry to "
                             L"bring its data in.")
                        .c_str(),
                    title.c_str(),
                    MB_RETRYCANCEL | MB_ICONINFORMATION) != IDRETRY) {
      return;
    }
  }
  ImportOutcome outcome;
  RunWithProgressWindow(*mode, outcome);
  if (outcome.error) {
    LogError() << "Importing the direct download's data failed: "
               << outcome.error->message;
    MessageBoxW(
        nullptr,
        (title + L" could not bring in the direct download's data. " +
         L"Nothing of it was changed.\n\n" + Utf8ToWide(outcome.error->message))
            .c_str(),
        title.c_str(), MB_OK | MB_ICONWARNING);
    return;
  }
  if (outcome.old_folder_kept) {
    MessageBoxW(nullptr,
                (title +
                 L" copied the direct download's data but could not "
                 L"delete its folder " +
                 DirectDownloadDataDirectory().wstring() +
                 L". Delete it yourself if you no longer need it.")
                    .c_str(),
                title.c_str(), MB_OK | MB_ICONINFORMATION);
  }
  if (*mode == DataImportMode::kMove && IsInstallerCopyInstalled()) {
    OpenInstalledAppsSettings();
  }
}

}  // namespace brouk::win
