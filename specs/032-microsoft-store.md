# 032 – Microsoft Store

- **Status:** in-progress
- **Priority:** P2
- **Depends on:** 004 (data folder), 010 (default browser), 016 (help), 019 (releases), 023 (licences), 024 (Windows app)

## Goal

Windows releases are not code-signed (024), so SmartScreen warns about the direct download, and company computers often block unsigned installers or allow only apps from the Microsoft Store. The Store signs the packages it publishes, so Brouk can be installed from it without warnings and without a certificate of Brouk's own. The Store also updates the app. For colleagues, "get it from the Store" is the simplest way to install it.

The Store is an **additional** way to get Brouk. The direct download (installer and Brouk's own updater, 024 W4) stays as it is (user's decision of 2026-11-19).

## Description

### Package

- Every Windows release also gets an **MSIX package** (`Brouk-<version>-x64.msix`), built in the same `windows-release.yml` run as the installer, from the same tested files. The package is a full-trust desktop app (`runFullTrust`), as Firefox's Store package is.
- The package's version is the release version with a fourth part of 0 (`0.6.0` → `0.6.0.0`), because the Store requires four parts with the last one 0. The version still comes from `APP_VERSION` only (CLAUDE.md).
- Name, publisher and the identity the Store assigns (package name, publisher ID) live in one place, next to the other app names (CMake variables), and are not hard-coded elsewhere.
- The package's icons are made from `branding/` by `scripts/make_icons`, at the sizes the Store requires.

### Data folder

- The Store app uses the **same data folder** as the direct download, `%LOCALAPPDATA%\Brouk\` (user's decision of 2026-11-19), so help, "Show the settings file" and profile export work the same, and a user who switches between the two keeps their data.
- MSIX normally redirects an app's writes to `%LOCALAPPDATA%` into a hidden folder of the package. The package turns this off for Brouk's data folder (`desktop6:FileSystemWriteVirtualization` disabled, which needs the restricted capability `unvirtualizedResources`, justified at submission). The data stays when the Store app is uninstalled, as with the installer.

### Updates

- The Store app is updated by the Store. Brouk's own updater is switched off in the Store app: it never checks the Windows feed and never downloads an installer (Store policy forbids updating outside the Store).
- Settings → Updates and About show "Updated by the Microsoft Store" instead of the channel choice and "Check for updates".
- Brouk knows it runs from the Store package from its package identity at runtime (`GetCurrentPackageFullName`), not from a build flag, so the installer and the package contain the same `brouk.exe` and `brouk.dll`.

### Default browser and links

- The package declares Brouk as a browser in its manifest: the `http` and `https` protocols and the `.htm`, `.html`, `.shtml`, `.xht`, `.xhtml` file types, opening with `--single-argument` as the installer's registration does (010, 024). The Store app does not write the HKCU registration that the installer writes, because a package's install folder changes with every version.
- Settings → Links → "Make Brouk the default browser" opens Windows Settings at the Store app's default apps page, as for the installer.
- A second start (a link from another app) reaches the running Brouk as today (`BroukRunning` mutex).

### Both installed

- When the Store app starts for the first time and the installer's Brouk is installed (its uninstall entry for the current user exists), it offers to uninstall the installer's Brouk: "Brouk is also installed from the direct download. Uninstall that copy? Your data stays." with **Uninstall** and **Keep both**. Uninstall runs the installer's uninstaller silently; the data folder stays. Keep both is remembered and the question is not asked again.
- If both are kept, both work with the same data, one at a time (`BroukRunning`).

### Releases

- `scripts/release_windows` (019, 024) also submits the MSIX package to the Store, from the Intel Mac, after the release is published, with the Microsoft Store Developer CLI (`msstore`, pinned in `scripts/install_tools`) through an Entra app registration linked to the Partner Center account. Its credentials go only into the keychain, as the update signing key does. No manual steps in Partner Center per release.
- Only **stable** releases go to the Store. Beta releases stay on the direct download.
- The Store certifies each submission (usually 1–3 days). A release counts as published when the direct download is published; the Store version simply follows when certified, and the release is not held back for it. A failed certification does not block the direct download; the script reports it.

### Store listing

- The publisher display name is the user's own name (the individual developer account). The listing is in **English** only.
- The listing's texts (short and long description, features, "what's new" from `CHANGELOG.md`) and screenshots live in the repository (`store/windows/`), so they are reviewed like other changes and follow the pitch deck.
- A **privacy policy** page is published in the releases repository (GitHub Pages) and linked from the listing and from help. It states what Brouk sends anywhere and to whom (search suggestions, update checks, ad block list downloads, Bitwarden), and that Brouk sends nothing to its developer.
- The age rating comes from the Store's questionnaire (a browser: unrestricted web access).

### Help

- Help → Installing (`#installing`) and Updates (`#updates`) describe both ways on Windows: the Store and the direct download, and that the Store app is updated by the Store.

## Acceptance criteria

- [ ] Every Windows release run builds `Brouk-<version>-x64.msix` from the same files as the installer; the Windows App Certification Kit (WACK) passes on it in CI.
- [ ] CI installs the package (signed with a throwaway test certificate made in the run), runs the Windows smoke test (024) against it and uninstalls it.
- [ ] The Store app reads and writes its settings, profiles and Chromium data in `%LOCALAPPDATA%\Brouk\`; data written by the installer's Brouk is used by the Store app and the other way round.
- [ ] At the first start of the Store app with the installer's Brouk installed, Brouk offers to uninstall that copy; Uninstall removes it and keeps `%LOCALAPPDATA%\Brouk\`, Keep both is not asked again.
- [ ] Uninstalling the Store app leaves `%LOCALAPPDATA%\Brouk\` in place.
- [ ] The Store app never checks for or downloads updates of its own; Settings → Updates and About say it is updated by the Microsoft Store. The installer's Brouk behaves as before.
- [ ] The Store app can be made the default browser from Settings → Links; then a link from another app and a double-clicked `.html` file open in Brouk, routed as in 010.
- [ ] `scripts/release_windows <version>` for a stable release submits the package to the Store without manual steps in Partner Center; for a beta release it does not.
- [ ] A Store submission passes certification, and Brouk installs from the Store on Windows 10 and 11 x64 without a SmartScreen warning.
- [ ] Updating through the Store keeps the user's windows and tabs (015).
- [ ] The privacy policy page is published and linked from the listing and from help; the listing's texts and screenshots are in `store/windows/`.
- [ ] Help describes installing and updating from the Store; the pitch deck's "Where it stands" shows the Store.

## Out of scope

- The Mac App Store (it requires the App Sandbox, which conflicts with Bitwarden CLI, link routing and other features; a separate spec if ever wanted).
- winget (a separate spec; it can point to the direct download or to the Store).
- Windows on ARM (002 open question).
- Code-signing the direct download (024: stays unsigned).
- Moving existing installer users to the Store app automatically.

## Configuration

None. Settings → Updates shows no choices in the Store app.

## Open questions

None.

## Notes

- 2026-11-19: created at the user's request. Steps outside the repository, done by the user: create an individual developer account in Partner Center (free for individuals, with identity verification), reserve the name "Brouk", fill in the age-rating questionnaire, and justify the restricted capabilities `runFullTrust` and `unvirtualizedResources` at the first submission.
- Things to check while implementing:
  - Brouk's `brouk.exe` is CEF's `bootstrap.exe`, which loads `brouk.dll` (024). Check in the CEF headers and docs whether the bootstrap's signature checks of the DLL behave differently when the Store signs the package (the Store signs the package, not the individual files).
  - The Chromium sandbox, Crashpad, Windows Hello, Credential Manager and launching `bw.exe` (008) inside a full-trust package.
  - Registry writes of a packaged app are virtualized; any HKCU writes Brouk makes apart from the browser registration (e.g. for the second start or the updater) must still work or be skipped in the Store app.
  - Whether the manifest's protocol declaration can pass `--single-argument` before the URL (manifest schema in the Windows SDK); if not, how the Store app keeps a link from adding switches.
- 2026-11-19 (user's decisions): the Store app offers to uninstall the installer's copy (keeping the data); only stable releases go to the Store; submission is automated (`msstore` from the Intel Mac); publisher display name is the user's name; listing in English only; the Store simply follows a release, which is not held back for certification. Status set to `ready`.
- 2026-11-21 (implementation, part 1 – the package): `scripts/package_msix` builds `Brouk-<version>-x64.msix` with the SDK's `makeappx` from the built app folder (as the installer ships it: no `.pdb`, no `crash_reporter.cfg`), the logos (`src/platform/win/package/Assets`, written by `scripts/make_icons`) and the manifest (`src/platform/win/package/AppxManifest.xml.in`, filled from `CMakeLists.txt`). The package identity (`APP_STORE_IDENTITY_NAME`, `APP_STORE_PUBLISHER`, `APP_STORE_PUBLISHER_DISPLAY_NAME`) has placeholders until Partner Center assigns it. File system write virtualization is off for the whole package (simpler than excluding one folder, which needs Windows 11), so the minimum is Windows 10 2004. An execution alias (`brouk`) starts the Store app from a terminal. Every Windows CI run and every release run builds the package and tests it (`scripts/test_windows_msix.ps1`: signed with a throwaway certificate, installed, `--version` through the alias, a start that must create `%LOCALAPPDATA%\Brouk` and not the package's private folder, uninstall keeps the data). Not yet: WACK, the runtime changes (updater off, settings text, default browser, the offer to uninstall the installer's copy), listing, privacy policy, help, submission.
