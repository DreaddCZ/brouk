#ifndef BROUK_PLATFORM_WIN_DATA_PATHS_H_
#define BROUK_PLATFORM_WIN_DATA_PATHS_H_

#include <filesystem>
#include <string_view>

namespace brouk::win {

// Brouk's data folder, %LOCALAPPDATA%\Brouk (024: where Chrome keeps its data
// too; not the roaming %APPDATA%, as Chromium's data is large and tied to
// the machine). Settings and profiles live here as on macOS (004, 005).
// The Store app (032) keeps its own in its package's folder
// (StoreAppDataDirectory), as the Store lets a package write only there.
// Created if missing.
std::filesystem::path AppDataDirectory();

// The direct download's data folder, %LOCALAPPDATA%\Brouk, also when the
// Store app asks (to import from it). Not created.
std::filesystem::path DirectDownloadDataDirectory();

// The Store app's package folder for its own files
// (%LOCALAPPDATA%\Packages\<package family>\LocalState) and its data
// folder in it (<LocalState>\Brouk); empty outside the package. Created if
// missing.
std::filesystem::path StoreAppStateDirectory();

// Chromium's data root (CefSettings::root_cache_path): <data>\web. Each
// profile's web data must be a direct child of it (see the macOS
// data_paths.h).
std::filesystem::path WebDataRootDirectory();
std::filesystem::path ProfileWebDataDirectory(std::string_view profile_id);

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_DATA_PATHS_H_
