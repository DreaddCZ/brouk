#ifndef BROUK_PLATFORM_WIN_STORE_PACKAGE_H_
#define BROUK_PLATFORM_WIN_STORE_PACKAGE_H_

#include <string>

namespace brouk::win {

// Brouk from the Microsoft Store (032). The Store app and the installer's
// Brouk are the same brouk.exe and brouk.dll; which one runs is known from
// the package identity Windows gives the Store app, not from a build flag.
// Any thread.

// Whether this Brouk runs as the Store app (with a package identity).
bool IsStoreApp();

// The Store app's package family name ("Radekevk.Brouk_dkfx2a54qv0ce"); ""
// outside the package.
std::wstring StorePackageFamilyName();

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

// Whether the installer's Brouk (024, W4) is installed for this user: its
// uninstall entry exists.
bool IsInstallerCopyInstalled();

// Opens Windows' Settings at the installed apps, where the installer's
// Brouk is uninstalled. The Store app does not run its uninstaller itself:
// inside the package the uninstaller's registry changes would be redirected
// into the package and never reach the real registration.
void OpenInstalledAppsSettings();

// Starts the Store app again (a restart for the data import); false outside
// the package or if it could not be started.
bool RelaunchStoreApp();

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_STORE_PACKAGE_H_
