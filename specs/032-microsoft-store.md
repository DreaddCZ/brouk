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

- The Store app keeps its data in **its own folder**, the package's `%LOCALAPPDATA%\Packages\<package family>\LocalState\Brouk\` (user's decision of 2026-11-26, after the Store refused `unvirtualizedResources`; it replaces the shared folder decided on 2026-11-19). The package uses no restricted capability apart from `runFullTrust`. Help, "Show the settings file", "Show log" and profile export use this folder in the Store app.
- Windows deletes this folder when the Store app is uninstalled, as for every Store app. Profile export (005) keeps a copy.
- The Store app's keys in Credential Manager (008, 009) have their own names ("Brouk Keys (Microsoft Store)…"), so deleting a profile in one copy never removes a key the other copy still uses.

### Updates

- The Store app is updated by the Store. Brouk's own updater is switched off in the Store app: it never checks the Windows feed and never downloads an installer (Store policy forbids updating outside the Store).
- Settings → Updates and About show "Updated by the Microsoft Store" instead of the channel choice and "Check for updates".
- Brouk knows it runs from the Store package from its package identity at runtime (`GetCurrentPackageFullName`), not from a build flag, so the installer and the package contain the same `brouk.exe` and `brouk.dll`.

### Default browser and links

- The package declares Brouk as a browser in its manifest: the `http` and `https` protocols and the `.htm`, `.html`, `.shtml`, `.xht`, `.xhtml` file types, opening with `--single-argument` as the installer's registration does (010, 024). The Store app does not write the HKCU registration that the installer writes, because a package's install folder changes with every version.
- Settings → Links → "Make Brouk the default browser" opens Windows Settings at the Store app's default apps page, as for the installer.
- A second start (a link from another app) reaches the running Brouk as today (`BroukRunning` mutex).

### Both installed

- Installing the Store app does not replace the direct download: both are installed, each with its own data, and both may run at the same time.
- When the Store app starts for the first time and the direct download's data folder `%LOCALAPPDATA%\Brouk\` exists, it offers to bring that data in: "Brouk from the direct download has data on this computer. Bring its profiles, settings, history and passwords into this copy?" with **Move**, **Copy** and **Not now**. Not now is remembered and the question is not asked again.
- **Copy** copies the direct download's data folder (without caches, logs and downloaded updates) and its keys in Credential Manager into the Store app; the direct download keeps its own. **Move** does the same, then deletes the direct download's data folder and keys, and opens Windows' Installed apps page when the direct download is still installed, so the user can uninstall it there.
- The import replaces the Store app's own data. Brouk restarts for it and imports before it opens any data; if the direct download is running, Brouk asks to quit it first (**Retry**, **Cancel**). A failed import leaves the Store app's data as it was and says what failed.
- The same import is in Settings → Advanced → **Import from the direct download…** in the Store app, while the direct download's data folder exists; it asks Move, Copy or Cancel, saying that it replaces this copy's data.

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
- [ ] The package declares no restricted capability apart from `runFullTrust`. The Store app reads and writes its settings, profiles and Chromium data in its package's `LocalState\Brouk\` and never writes `%LOCALAPPDATA%\Brouk\`.
- [ ] At the first start of the Store app with the direct download's data present, Brouk offers Move, Copy and Not now; after Copy the Store app has the same profiles, settings, history, bookmarks and saved passwords and the direct download still has its own; after Move the same, the direct download's data folder is gone and Windows' Installed apps page opens; Not now is not asked again.
- [ ] Settings → Advanced → Import from the direct download… does the same at any time, replacing the Store app's data; while the direct download runs, Brouk asks to quit it first.
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
- Merging the direct download's data into data the Store app already has (the import replaces it; profile export and import (005) move single profiles).

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
- 2026-11-22 (implementation, part 2 – the Store app's behaviour): `src/platform/win/store_package.{h,cc}`: the Store app is known by its package identity (`GetCurrentPackageFullName`). In it Brouk creates no updater, "Check for updates…" offers to open the Store's updates page (`ms-windows-store://downloadsandupdates`), Settings → Updates shows "Brouk from the Microsoft Store is updated by the Store." in place of channel and mode (`updatesByStore` in the settings state), no HKCU browser registration is refreshed, and "Make Brouk the default browser" opens Windows' default apps page for the Store app (`registeredAUMID=<package family>!Brouk`). Whether it is the default: the http and https ProgIDs' `Application\AppUserModelID` is the Store app's. Windows has no About window, so "Check for updates…" stands for About here. The offer to uninstall the installer's copy is a Yes/No message box (Windows' message box has no custom button labels; the text says what Yes and No do) shown 2 s after the start, when the installer's uninstall entry exists; No writes `store-keeps-installer-copy` in the data folder. Registry write virtualization is off too (the uninstaller runs inside the package's context and must remove the real HKCU registration). The installer's AppId is repeated in `store_package.cc`, with a note in `brouk.iss`.
- 2026-11-22 (implementation, part 3 – listing): the listing's texts are in `store/windows/listing.md`, the privacy policy in `store/windows/privacy.md` (to be published in the releases repository's Pages as `/privacy`). `msstore` 0.4.4 is pinned in `scripts/install_tools` for the Macs; it keeps the client secret in the macOS Keychain itself (`msstore reconfigure`, run once by the user, asking for the values), so Brouk's scripts never handle the secret. Its `publish` options are shown only once it is configured, so the release step is written after the account exists.
- 2026-11-22: Partner Center assigned the identity: package name `Radekevk.Brouk`, publisher `CN=0DD652F3-DAAB-4C46-B018-097F29A54770`, package family `Radekevk.Brouk_dkfx2a54qv0ce`, Store ID `9PDWZPW6RZ0Z` (https://apps.microsoft.com/detail/9PDWZPW6RZ0Z). They replace the placeholders in `CMakeLists.txt` (`APP_STORE_*`, with `APP_STORE_ID` new).
- 2026-11-22: released 0.6.0 with the package; the first submission was made by hand in Partner Center and is in certification. Left: WACK in CI; after the listing is live, help's Installing section names the Store and the pitch deck's "Where it stands" shows it; the manual tests of the Store app (Settings → Updates, Check for updates, default browser, the offer to uninstall the installer's copy, a Store update keeping the session, no SmartScreen warning); then status `done`.
- 2026-11-26: certification refused the restricted capability `unvirtualizedResources` (asked for: why no less privileged way works). The user decided to drop it rather than ask again: the Store app gets its own data folder and imports the direct download's data on request (Move, Copy, Not now at the first start; also in Settings). "Data folder" and "Both installed" above and their acceptance criteria were changed with the user's approval. Choices made while specifying it: the import replaces the Store app's data (no merge), runs at a restart before any data is opened, leaves out Chromium's caches, logs and downloaded updates, and copies the keys in Credential Manager under the Store app's own names. Registry write virtualization stays on too (its switch needs the same capability), so the Store app no longer runs the direct download's uninstaller (its HKCU deletes would be redirected); Move opens Windows' Installed apps instead.
