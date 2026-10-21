# 001 – Technology stack

- **Status:** draft
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
- **Profiles (005):** our own implementation. Each profile maps to its own CEF request context with its own storage directory (cookies, cache, local storage isolated). Profile management, switching and UI are ours — this also makes per-tab isolation (Firefox-style containers) possible.
- **Developer tools (006):** phase 1 ships the standard Chromium DevTools frontend that CEF includes. Our own tooling is built on top of the Chrome DevTools Protocol (exposed by CEF) and replaces or extends panels step by step. Rewriting all of DevTools from scratch is not a goal.
- **Password manager (008) and autofill (009):** our own implementation — injected scripts detect login/address forms; secrets are stored encrypted via the OS keychain (macOS Keychain, later Windows Credential Manager/DPAPI); save/fill prompts are our own UI.
- **Updates:** the app updates itself automatically and regularly; the user never maintains it manually (see "Update process" below).

- **UI shell: hybrid** (decided 2026-10-21).
  - **Native** (Swift/AppKit on macOS, the platform's native toolkit on Windows later): window frames, tab strip, toolbar and address bar with its suggestions dropdown, menus, the window picker (010), and OS integration (displays, Spaces, Touch ID, full-screen). Reason: speed, low memory, native feel, and window/display/Space awareness is native code in any case.
  - **HTML/TypeScript pages rendered in CEF** for content-heavy internal pages: settings, history, bookmarks/downloads views, password manager page, profile manager and our own developer tools. Reason: one implementation shared by macOS and Windows, easy theming, fast iteration.
  - The window/tab model lives in the shared core, independent of the UI layer.

### Update process

Chromium ships a new stable release every ~4 weeks plus security fixes in between, and CEF follows with new builds. Staying current means:

1. **Automated CEF bump (CI):** a GitHub Actions workflow detects new CEF stable builds, updates the pinned CEF version, builds, runs tests, signs and notarizes the app. API breaks are fixed in a pull request (Claude Code can do this).
2. **Automatic delivery to the user:** an in-app updater — Sparkle on macOS, WinSparkle or equivalent on Windows — checks a signed update feed and installs new versions in the background.
3. **Evaluate** the CEF repository's new installer library (automatic CEF version management with background updates) before choosing the final updater; check its maturity first.

## Acceptance criteria

- [x] The base technology is chosen and recorded in this spec and in `CLAUDE.md`.
- [ ] Implementation language(s), UI toolkit and build system are recorded in this spec and in `CLAUDE.md`.
- [ ] A "hello world" build (CEF, Alloy style) runs on macOS and opens a web page.
- [ ] CI builds, signs and notarizes the macOS app.
- [ ] A new CEF stable release is picked up by CI without manual steps other than merging the update PR.
- [ ] An installed app updates itself to a newer published version without user involvement beyond (optionally) a restart.

## Out of scope

- Writing our own rendering or JavaScript engine.
- Mobile platforms (see 002).

## Configuration

Update channel (stable by default), automatic install of updates (on by default), check interval.

## Open questions

- Implementation language for the core (C++ is CEF's native API; Swift/C++ interop on macOS; Rust bindings exist).
- Build system (CMake is CEF's default).
- Chrome extensions: support at all? (Limited in Alloy style.)
- Release cadence: ship every CEF stable update, or batch them (security trade-off)?
- Update feed hosting (GitHub Releases or own server).

## Notes

- Superseded leaning (before 2026-10-21): Option A, a Chromium fork based on ungoogled-chromium's patch set.
- CEF runtime styles: Chrome style provides the full Chrome UI layer; Alloy style provides less built-in functionality but more client callbacks and off-screen rendering. Styles are chosen per browser/window and can be mixed.
- UI shell trade-offs considered: an all-HTML UI (Vivaldi-style) would share one UI across platforms but costs an extra renderer process per UI surface, makes overlays like the address bar dropdown awkward to layer, and does not feel native; an all-native UI needs every page written per platform. The hybrid keeps the always-visible parts native and shares the content-heavy pages.
