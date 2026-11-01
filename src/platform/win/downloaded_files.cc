#include "platform/win/downloaded_files.h"

#include <windows.h>

#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <filesystem>
#include <system_error>

#include "platform/win/wide_string.h"

namespace brouk::win {

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

}  // namespace brouk::win
