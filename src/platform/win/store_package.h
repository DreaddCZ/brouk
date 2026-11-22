#ifndef BROUK_PLATFORM_WIN_STORE_PACKAGE_H_
#define BROUK_PLATFORM_WIN_STORE_PACKAGE_H_

#include <filesystem>
#include <optional>
#include <string>

namespace brouk::win {

// Brouk from the Microsoft Store (032). The Store app and the installer's
// Brouk are the same brouk.exe and brouk.dll; which one runs is known from
// the package identity Windows gives the Store app, not from a build flag.
// Any thread.

// Whether this Brouk runs as the Store app (with a package identity).
bool IsStoreApp();

// The Store app's AppUserModelID ("<package family name>!Brouk"), which
// Windows uses for its default-app choices; "" outside the package.
std::wstring StoreAppUserModelId();

// Whether http and https links open in the Store app now. Windows gives a
// packaged app's links a ProgID of its own (AppX…), which names the app.
bool IsStoreAppDefaultBrowser();

// Opens Windows' Settings at the Store app's default apps page (Windows 11),
// or at the default apps (Windows 10).
void OpenStoreAppDefaultAppsSettings();

// Opens the Microsoft Store's updates page ("Check for updates…").
void OpenStoreUpdates();

// The uninstaller of the installer's Brouk for this user (024, W4), when
// that copy is installed too; else nothing.
std::optional<std::filesystem::path> InstallerCopyUninstaller();

// Runs that uninstaller silently and returns at once (it copies itself and
// waits for nothing of Brouk's). Brouk's data folder stays, as with every
// uninstall. False if it could not be started.
bool UninstallInstallerCopy(const std::filesystem::path& uninstaller);

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_STORE_PACKAGE_H_
