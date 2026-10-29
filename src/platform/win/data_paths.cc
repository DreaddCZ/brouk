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
      return EnsureDirectory(path);
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
