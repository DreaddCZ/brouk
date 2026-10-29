#include "platform/win/data_paths.h"

#include <windows.h>

#include <shlobj.h>

#include <cstdlib>
#include <system_error>

#include "brouk/app_info.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

const std::filesystem::path& EnsureDirectory(
    const std::filesystem::path& path) {
  // %LOCALAPPDATA% is readable by its user only, and new folders inherit
  // that, so no permissions are set here (unlike 0700 on macOS).
  std::error_code ignored;
  std::filesystem::create_directories(path, ignored);
  return path;
}

// The long form of an existing path: Chromium cannot open its databases
// under a short 8.3 name such as C:\Users\RUNNER~1 (the temporary folder of
// some accounts).
std::filesystem::path LongPath(const std::filesystem::path& path) {
  const DWORD size = GetLongPathNameW(path.c_str(), nullptr, 0);
  if (size == 0) {
    return path;
  }
  std::wstring long_path(size, L'\0');
  const DWORD written = GetLongPathNameW(path.c_str(), long_path.data(), size);
  if (written == 0 || written >= size) {
    return path;
  }
  long_path.resize(written);
  return long_path;
}

std::filesystem::path LocalAppData() {
  PWSTR folder = nullptr;
  std::filesystem::path path;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE,
                                     nullptr, &folder))) {
    path = folder;
  }
  CoTaskMemFree(folder);
  return path;
}

}  // namespace

std::filesystem::path AppDataDirectory() {
  // For developer test runs only: a separate data folder lets a test instance
  // run next to the user's own (CEF allows one instance per data folder).
  if (const wchar_t* dev_dir = _wgetenv(L"BROUK_DEV_DATA_DIR")) {
    std::filesystem::path path(dev_dir);
    if (path.is_absolute()) {
      return LongPath(EnsureDirectory(path));
    }
  }
  return EnsureDirectory(LocalAppData() / Utf8ToWide(kAppDataDirName));
}

std::filesystem::path WebDataRootDirectory() {
  return EnsureDirectory(AppDataDirectory() / L"web");
}

std::filesystem::path ProfileWebDataDirectory(std::string_view profile_id) {
  return EnsureDirectory(WebDataRootDirectory() / Utf8ToWide(profile_id));
}

}  // namespace brouk::win
