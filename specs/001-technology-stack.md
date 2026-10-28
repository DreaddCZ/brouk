# 001 – Technology stack

- **Status:** in-progress
- **Priority:** P1
- **Depends on:** none (every other spec depends on this one)

## Goal

Choose the base technology for a Chromium-based browser that is lightweight, fully configurable, runs on macOS first (Windows and mobile later), and gives us profiles, developer tools, ad blocking, a password manager and autofill — implemented our own way — while staying up to date with Chromium security fixes through automatic updates instead of manual maintenance.

## Description

The browser must use Chromium (Blink + V8) as its rendering engine. Options considered:

| Option | What it is | Pros | Cons |
|---|---|---|---|
| **A. Chromium fork** | Our own patch set on top of upstream Chromium (the approach of Brave, Vivaldi, ungoogled-chromium) | DevTools, profiles, password manager, autofill, extensions and web compatibility come built in; full control over everything | Huge codebase (C++); checkout and build need a powerful machine, lots of disk and hours per full build; patches must be rebased on every Chromium release (~every 4 weeks) |
| **B. CEF + own UI** | Chromium Embedded Framework with our own browser shell | Prebuilt binaries; small app codebase; fast builds; total UI freedom | No built-in password manager or autofill; limited extension support; adblock, password manager and autofill must be built ourselves |
| **C. Electron** | Chromium + Node.js app framework | Fastest to prototype; built-in auto-updater; TypeScript | Heavier memory use, conflicts with the "lightweight" goal; lags behind Chromium security fixes; less low-level control |

Platform-native webviews (e.g. WKWebView on macOS) are excluded because they are not Chromium.

## Decision (2026-10-21): Option B — CEF + our own browser shell

**Why:** the fork's main advantage was getting Chrome's profiles, DevTools and password manager for free. We want all three to work *differently* from Chrome, so with a fork we would be ripping out and rewriting large parts of Chrome anyway, while still rebasing a patch set on every Chromium release. CEF gives us the engine and leaves the browser around it to us, with prebuilt binaries and fast builds.

### Decided details

- **Engine:** CEF, using its prebuilt binary distributions (no Chromium source build).
- **Runtime style:** Chrome bootstrap (the only bootstrap since CEF M128) with **Alloy style** browsers for web content. Chrome style would bring Chrome's own UI pieces (password prompts, autofill, profile UI) that would compete with ours.
- **Profiles (005):** our own implementation. Each profile maps to its own CEF request context with its own storage directory (cookies, cache, local storage isolated). Profile management, switching and UI are ours — this also lets tabs of several profiles share one window (see 005).
- **Developer tools (006):** phase 1 ships the standard Chromium DevTools frontend that CEF includes. Our own tooling is built on top of the Chrome DevTools Protocol (exposed by CEF) and replaces or extends panels step by step. Rewriting all of DevTools from scratch is not a goal.
- **Password manager (008) and autofill (009):** our own implementation — injected scripts detect login/address forms; secrets are stored encrypted via the OS keychain (macOS Keychain; Windows Credential Manager, with Windows Hello for biometric unlock); save/fill prompts are our own UI.
- **Updates:** the app updates itself automatically and regularly; the user never maintains it manually (see "Update process" below).

- **UI shell: hybrid** (decided 2026-10-21).
  - **Native** (Objective-C++/AppKit on macOS, C++ with the Win32 API on Windows): window frames, tab strip, toolbar and address bar with its suggestions dropdown, menus, the link routing and window picker dialog (010), and OS integration (displays, Spaces, Touch ID, full-screen). Reason: speed, low memory, native feel, and window/display/Space awareness is native code in any case.
  - **HTML/TypeScript pages rendered in CEF** for content-heavy internal pages: settings, history, bookmarks/downloads views, password manager page, profile manager and our own developer tools. Reason: one implementation shared by macOS and Windows, easy theming, fast iteration.
  - The window/tab model lives in the shared core, independent of the UI layer.

- **Languages** (decided 2026-10-21), chosen for how reliably Claude Code can write and maintain them:
  - **C++ (C++20)** for the core: CEF integration, window/tab model, profiles, password and autofill storage, update logic. CEF's own API, samples and most examples are C++, and the language changes slowly.
  - **Objective-C++ (AppKit)** for the native macOS UI. It mixes directly with C++ (no bridging layer), AppKit's Objective-C API is stable and well documented, and Chrome's and CEF's own Mac code is written this way. Small Swift files are allowed only when a macOS API is not available from Objective-C.
  - **TypeScript** for the HTML pages, plain or with at most one long-established library, with pinned versions. No fast-changing frameworks.
  - **C++ with the Win32 API** for the native Windows UI (decided 2026-10-28). It is the same language as the core (no bridging layer), Chromium's and CEF's own Windows code is written this way, it adds no runtime or framework to install, and it keeps the app small and fast to start. Windows 10/11 visual details (dark mode, Mica, rounded corners) come through the DWM and uxtheme APIs. No WinUI, WPF, MFC or Qt.
  - **Exception (2026-10-23):** Brave's `adblock-rust` (MPL-2.0) is used as a library through a C interface for ad blocking (007). The Rust toolchain is pinned and used only to build that library; at most a thin wrapper crate is written by us.

- **Build system and tests** (decided 2026-10-21):
  - **CMake + Ninja** (CEF's binary distribution and samples use CMake; Ninja keeps incremental builds fast).
  - A script downloads the **pinned CEF version** and verifies its checksum, so every build uses the same CEF.
  - TypeScript compiled with the standard TypeScript compiler (`tsc`) only, no bundler.
  - Tests: **GoogleTest** for C++, **Node's built-in test runner** for TypeScript.
  - The core (everything that is not platform UI) builds and passes its tests on **Linux** too, so Claude Code cloud sessions (Linux) and cheap Linux CI can work on and verify it. macOS UI work and macOS builds need a Mac.
- **Extensions** (decided 2026-10-21): **no Chrome extension support.** The user rarely uses extensions. The main wish, uBlock-style blocking, is covered by built-in ad blocking (007). If extensions are ever wanted, a small extension mechanism of our own is enough (no spec yet).
- **Phased build and release setup** (decided 2026-10-21):
  - **Phase 1 — personal use (first weeks):** builds are made locally on the user's Macs (both architectures; the Intel build can also be cross-compiled on an Apple Silicon Mac). GitHub Actions builds and tests the portable code on Linux on every push and on Windows for every pull request, and builds and tests the whole macOS app (Apple Silicon) for every pull request (free since the repository is public, see Notes). No Apple Developer Program, notarization or public update feed yet; updating means rebuilding locally.
  - **Phase 2 — colleagues:** when the user is satisfied, the browser is shared with colleagues. Then: Apple Developer Program membership for signing and notarization, macOS builds in CI (at least the macOS-specific part), a public update location (public repo or a separately hosted update feed — private-repo releases cannot be downloaded by the updater), and automatic updates as described below.

### Update process

Applies from phase 2 (see above).


Chromium ships a new stable release every ~4 weeks plus security fixes in between, and CEF follows with new builds. Staying current means:

1. **Automated CEF bump (CI):** a GitHub Actions workflow detects new CEF stable builds, updates the pinned CEF version, builds, runs tests, signs and notarizes the app — on macOS once for Apple Silicon and once for Intel (see 002). API breaks are fixed in a pull request (Claude Code can do this).
2. **Automatic delivery to the user:** an in-app updater — Sparkle on macOS, WinSparkle on Windows — checks a signed update feed and installs new versions in the background.
3. **Release cadence:** every CEF patch release (mostly Chromium security fixes, no API change) is built and shipped automatically. Each major release (new Chromium version, ~every 4 weeks) is shipped as soon as its update PR — with any fixes for CEF API changes — is merged. No batching.
4. **Update feed hosting:** decided at the start of phase 2. Preferred option (not final): a separate public "releases" repo holding only signed builds and the update feed (GitHub Releases, free); the source repo stays private. Alternatives: making the source repo public, or cloud storage (S3, Cloudflare R2).
5. **Evaluate** the CEF repository's new installer library (automatic CEF version management with background updates) before choosing the final updater; check its maturity first.

## Acceptance criteria

- [x] The base technology is chosen and recorded in this spec and in `CLAUDE.md`.
- [x] Implementation language(s), UI toolkit and build system are recorded in this spec and in `CLAUDE.md`.
- [ ] A "hello world" build (CEF, Alloy style) runs on macOS and opens a web page.
- [ ] Phase 1: one documented command builds the app locally on a Mac, for Apple Silicon and for Intel.
- [ ] Phase 1: GitHub Actions builds the core and runs its tests on Linux for every push/pull request.
- [ ] Phase 2: CI builds, signs and notarizes the macOS app.
- [ ] Phase 2: a new CEF stable release is picked up by CI without manual steps other than merging the update PR.
- [ ] Phase 2: an installed app updates itself to a newer published version without user involvement beyond (optionally) a restart.

## Out of scope

- Writing our own rendering or JavaScript engine.
- Mobile platforms (see 002).

## Configuration

Update channel (stable by default), automatic install of updates (on by default), check interval.

## Open questions

None. (Update feed hosting was decided on 2026-10-26, see Notes.)

## Notes

- 2026-10-21 (implementation start): pinned CEF 154.0.34 (Chromium 154.0.8037.98), the current stable build for macOS Intel, macOS Apple Silicon and Linux; the minimal binary distribution is enough (it contains headers, wrapper sources and the Release framework). Build tools are downloaded by `scripts/install_tools` into `.tools/` with checksums instead of Homebrew, because Homebrew no longer ships prebuilt packages for Intel Macs (building LLVM from source would take hours). See `docs/building.md`.
- 2026-10-25: `scripts/package_mac` makes a disk image per architecture (`build/dist/Brouk-<version>-<arch>.dmg`) for handing builds to others in phase 1: Release build, app plus Applications link plus a read-me on allowing the first launch (no Developer ID yet, so Gatekeeper blocks it until "Open Anyway"). Building it for the other architecture also fixed cross builds: the Rust library is now built for the target architecture (`cargo --target`), with the other Mac architecture's Rust standard library installed by `scripts/install_tools`. Phase 2 adds Developer ID signing and notarization to the same script.
- 2026-10-26: Developer ID signing and notarization added to `scripts/package_mac` (the user has the Apple Developer Program membership and a "Developer ID Application" certificate). Release images are signed with the hardened runtime and a secure timestamp; the app and the image are each notarized and stapled. Entitlements follow Chrome's (`src/platform/mac/entitlements/`): JIT for the renderer, GPU (SwiftShader) and plain helper (utility processes such as the proxy resolver run V8), unsigned executable memory and no library validation for the plugin helper, camera, microphone, location, Bluetooth, USB and printing for the app. No keychain access group: the keychain items (`src/platform/mac/keychain.mm`) use the file-based keychain, which needs none. Local builds are signed with the team's "Apple Development" certificate, without the hardened runtime (it would block the sanitizer runtime and debuggers): the keychain remembers apps by team ID, so rebuilds and the notarized releases no longer ask for keychain access. Developer ID cannot sign debug builds, since macOS refuses to load the sanitizer runtime into a Developer ID app ("Sanitizer load violates platform policy"). The self-signed "Brouk Local Dev" certificate remains the fallback without an Apple certificate. Building and notarizing in CI remain phase 2.
- 2026-10-26 (user's decision, see 019): update feed hosting ("Update process", point 4) is decided: a separate public releases repo (`DreaddCZ/brouk-releases`), with the Sparkle feeds on its GitHub Pages; the source repo stays private. Releases are built and tested on the user's Intel Mac rather than in CI (the GitHub Actions minutes ran out); automatic CEF updates in CI (point 1) are postponed.
- 2026-10-28 (user's decision): CEF update policy while automatic updates in CI (point 1) are postponed. A newer CEF patch (same major) is taken within 7 days of the first one appearing; a newer major within 28 days, and Brouk is never more than one CEF major behind. Every CEF update is followed by a Brouk release. Detection is automatic: `scripts/check_cef` (rules in `scripts/cef_versions.mjs`, unit-tested) compares the pinned version with CEF's build index and fails when an update is overdue; it runs in `scripts/sast`, so the weekly `security` workflow reports it. The bump itself is `scripts/update_cef` (checksums checked against CEF's index, `build/cef-include.diff` for reviewing API changes) followed by `docs/cef-updates.md` and the checklist `docs/testing/cef-update.md`.
- 2026-10-25: Windows CI added (weekly and on demand, user's decision): the portable code builds on Windows x64 with clang-cl and passes its tests. CEF's Windows distribution (windows64, same 154.0.34 build) and Windows versions of the pinned tools come from `scripts/fetch_cef` and `scripts/install_tools`. Details in 002's notes and `docs/building.md`.
- 2026-10-21: the app process cannot run under AddressSanitizer — the CEF framework traps at startup. Debug builds of the app use UndefinedBehaviorSanitizer only; the tests (everything testable without CEF) run under AddressSanitizer + UBSan.
- 2026-10-21: Chromium requires each profile's data folder to be a direct child of its data root (`root_cache_path`) and fills that root with install-wide files. Chromium's data therefore lives in `~/Library/Application Support/Brouk/web/` (`web/<profile-id>/` per profile), separate from our own files in `profiles/<profile-id>/` (004). Deleting a profile (005) must remove both folders.
- 2026-10-21 (decided with the user): ad-hoc signatures change with every build, so macOS asked again for the keychain item "Chromium Safe Storage" (and would for camera/microphone permissions). Local builds are now signed with a self-signed "Brouk Local Dev" certificate created once per Mac by `scripts/create_dev_certificate`; without it the build falls back to ad-hoc signing. Phase 2 replaces this with the Apple Developer ID certificate.

- 2026-10-21 (stack review): release cadence agreed (patch releases automatically, major releases after their PR is merged). Update feed hosting postponed to phase 2 — during the first weeks only the user tests the browser and reinstalls new versions manually. Spec set to `ready` for phase 1.

- Superseded leaning (before 2026-10-21): Option A, a Chromium fork based on ungoogled-chromium's patch set.
- CEF runtime styles: Chrome style provides the full Chrome UI layer; Alloy style provides less built-in functionality but more client callbacks and off-screen rendering. Styles are chosen per browser/window and can be mixed.
- UI shell trade-offs considered: an all-HTML UI (Vivaldi-style) would share one UI across platforms but costs an extra renderer process per UI surface, makes overlays like the address bar dropdown awkward to layer, and does not feel native; an all-native UI needs every page written per platform. The hybrid keeps the always-visible parts native and shares the content-heavy pages.
- Language trade-offs considered (2026-10-21): **Swift** changes yearly (Swift 6 strict concurrency, SwiftUI updates), which is where models produce outdated code, and its C++ interop is recent with little training data. **Rust** has the strongest compiler checks, but its CEF bindings are young, change with every CEF release and have little training data, its macOS UI crates change often, and builds are slower. **C++** risks memory bugs that compile but crash later; countered with strict warnings, sanitizers and tests in CI.
- CI cost context (2026-10-21): the repo is private; GitHub counts macOS minutes at 10x against the included minutes (free plan: 2,000 min/month ≈ 200 macOS minutes), with macOS overage around $0.062/min. GitHub's Intel macOS runners end in fall 2027, so the Intel build is compiled on Apple Silicon.
- 2026-10-25 (user's decision, see 019): releases for beta testers are built locally as today, smoke-tested on GitHub-hosted Macs from a draft release, and published in this repository only when the tests pass. CI therefore also runs short macOS test jobs (not builds) for each release.
- Extension context (2026-10-21): in CEF, Chrome extensions run only in Chrome-style browsers, which we avoid for web content. Chrome 139+ no longer runs Manifest V2 extensions, and the full uBlock Origin (MV2) was removed from the Chrome Web Store on 2026-08-31; only the more limited uBlock Origin Lite (MV3) remains for Chromium browsers. A built-in blocker is not bound by MV3 limits.
- 2026-10-28 (user's decision): the source repository is made public under the Apache License 2.0, as a new repository `DreaddCZ/brouk` with history checked for private data; the old private `DreaddCZ/web-browser` is archived. Reason: GitHub Actions minutes are free for public repositories, so CI is not limited by cost (the macOS minute limits above no longer apply to the source repository). The releases repository stays separate (019). Contribution rules: `CONTRIBUTING.md`; security reports: `SECURITY.md`.
- 2026-10-28 (user's decision): with the repository public, CI minutes are free, so the Windows job runs for every pull request and push to `main` instead of weekly, and a macOS job builds the whole app for Apple Silicon (signed ad-hoc), runs the tests under the sanitizers and runs clang-tidy including the macOS UI. Releases are still built, notarized and smoke-tested on the user's Intel Mac (019); the Intel build is not built in CI.
- 2026-10-28 (user's decision): the Windows UI is plain C++ on the Win32 API, in `src/platform/win/`. WinUI 3 was the alternative: it brings XAML controls and Fluent styling, but needs the Windows App SDK runtime, C++/WinRT and more memory. Win32 suits the lightweight goal, mixes directly with the C++ core, and matches how Chromium and CEF build their Windows UI. The Windows replacements for Sparkle and the keychain were decided the same day (next note).
- 2026-10-28 (user's decisions, for when Windows work starts):
  - **Updates (019): WinSparkle**, the Windows port of Sparkle (MIT, C/C++). It reads the same appcast feed format and verifies EdDSA signatures, so the releases repo gets a Windows feed next to the macOS ones; check that the WinSparkle version used supports EdDSA. It installs an update by running a **per-user installer** (planned: Inno Setup, into `%LocalAppData%`, so no admin rights are needed; many colleagues have none, 017).
  - **Code signing: the free signing for open-source projects (SignPath Foundation)**, from CI. Possible since the repository is public; it needs an application and builds made in CI from the public repository, so Windows releases are built on GitHub's Windows runners (free for public repositories), not on the user's Macs. Without signing, SmartScreen warns on every download.
  - **Secrets (008, 009): Windows Credential Manager** (`CredWrite`/`CredRead`, generic credentials), behind the same keychain interface as on macOS. Chosen over DPAPI files because company PCs are managed by policy, and Credential Manager is what such policies govern and what users and administrators can see. Each item holds at most 2.5 KB, enough for the keys stored.
  - **Biometric unlock: Windows Hello** (fingerprint, face or PIN) in place of Touch ID, e.g. for the Bitwarden session (008). It is a WinRT API, called from the Win32 C++ code.
- 2026-10-28 (user's decision): CI skips the linux, macos and windows builds for changes to specs and docs only, such as the spec of a future feature (`scripts/ci_changes.mjs`, tested). The skipped jobs count as passed for the required checks. Anything unclear builds: a failed check, a new branch, a manual run.
- 2026-10-28 (CEF update): moved to CEF 156.0.14 (Chromium 156.0.8078.48), 6 days after its release (`docs/cef-updates.md`). Its API changes need no code change: a new permission request type (ambient login) gets the generic prompt for other permissions, and the new keychain name settings are left empty, which keeps the keychain item used so far.
