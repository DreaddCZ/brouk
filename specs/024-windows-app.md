# 024 – Windows app

- **Status:** in-progress
- **Priority:** P2
- **Depends on:** 001, 002, and every spec it ports (003–016, 019, 020, 022, 023)

## Goal

Brouk on Windows 10 and 11 (x64) with the same features as on macOS, so the user and colleagues on Windows PCs can use it for work (002).

## Description

The Windows app is the existing portable core (`src/core/`, `src/cef/`, `src/renderer/`), the same internal pages (`web/`), and a new native shell in C++ on the Win32 API in `src/platform/win/` (decided in 001). The shell does what `src/platform/mac/` does on macOS: windows, the tab strip, toolbar and address bar, menus, popups and prompts, and the system integration (secret store, default browser, updates). Features behave as their specs describe; where a spec names a macOS control or shortcut, the Windows app uses the Windows equivalent (Ctrl for ⌘, a "⋮" menu at the right of the toolbar for the menu bar), as Chrome does on Windows.

It is built in stages, each ending with a build the user tests on Windows:

| Stage | Goal | Ports |
|---|---|---|
| **W1 – Browse the web** | One window with tabs, address bar with search, back/forward/reload, standard shortcuts, the 012 basics (dialogs, permissions, sign-ins, errors, find, zoom, print, full screen, downloads), the internal pages open in their windows | 003 (basic), 012, 016 |
| **W2 – Everyday browser** | Profiles side by side, settings with live reload, session restore, bookmarks bar, history, downloads list, task manager, command palette, closed tabs | 003, 004, 005, 011, 013, 014, 015, 020 |
| **W3 – Work features** | Passwords (built-in in Credential Manager, Bitwarden CLI, Windows Hello unlock), autofill, ad blocking with the shield, developer tools dock, link routing with the picker and Brouk as the default browser | 006, 007, 008, 009, 010 |
| **W4 – Release** | Per-user installer, WinSparkle updates from a Windows feed in the releases repo, SignPath signing, built in CI | 019, 023 |

## Acceptance criteria

- [ ] W1–W3: each feature's acceptance criteria hold on Windows 11 x64, except where its spec says the criterion is macOS-only; differences are recorded under Notes in that spec.
- [ ] The Windows app builds in CI on every pull request, and every pull request that changes `src/platform/win/` gives the user a build to test (a zip of the app as a workflow artifact).
- [ ] A smoke test on the Windows CI runner starts the app, loads the test pages and the internal pages, crashes a renderer and quits, like the macOS smoke test (019).
- [ ] User data lives in `%LOCALAPPDATA%\Brouk\` (settings, profiles, Chromium's data), as Chrome keeps its data in `%LOCALAPPDATA%\Google\Chrome\User Data`.
- [ ] Secrets go only to Windows Credential Manager; biometric unlock uses Windows Hello.
- [ ] The app follows the system light/dark mode and accent colour, scales on high-DPI displays (per-monitor DPI awareness v2), and its controls work with the keyboard and a screen reader (UI Automation names).
- [ ] W4: an installer installs per user without admin rights; the app updates itself through WinSparkle with EdDSA-signed updates; the installer and app are signed through SignPath.
- [ ] `src/core/` gains no Windows includes; Windows code reaches the core only through the interfaces in `src/core/` (CLAUDE.md).

## Out of scope

- Windows on ARM (002, deferred).
- Windows Store / MSIX and winget (distribution, 002 deferred).
- Linux as a user-facing platform.

## Configuration

The same settings as on macOS. Settings that only exist on one system are hidden on the other.

## Open questions

None.

## Notes

- 2026-10-29 (user's decisions):
  - **Testing:** the user tests in a Windows 11 x64 virtual machine on the Intel Mac (e.g. VMware Fusion, free for personal use). Claude Code checks each build in CI first (smoke test, screenshots on the Windows runner).
  - **Window frame:** the tab strip fills the title bar, like Chrome and Edge and like Brouk on macOS: a custom frame drawn by Brouk that keeps the system's minimise/maximise/close buttons, snap layouts on the maximise button, Aero Snap and the window menu.
  - **Order:** W1, W2, W3, W4 as in the table above.

- 2026-10-29: data location follows Chrome on Windows (`%LOCALAPPDATA%`, not the roaming `%APPDATA%`): Chromium's data is large and machine-specific, and the settings live with it as on macOS. This answers 004's open Windows location.
- The macOS UI is about 22,000 lines in 86 files (`src/platform/mac/`); the Windows shell is a port of comparable size, so it is split into many pull requests, one or a few features each.
- 2026-10-29 (implementation): Chromium's own profile, behind CEF's global request context (used only for background downloads: ad blocking lists, the breach check), lives in `web/Default`; the first Brouk profile (id `default`) in `web/default`. On Windows, whose file names ignore case, that is one folder: the two profiles locked each other's databases and Chromium showed "Profile error occurred". On Windows Brouk starts Chromium with `--profile-directory=Chromium`, so its profile has a folder of its own. On macOS the two share `web/Default` and have worked so far (macOS file locks do not block within one process); the same switch was tried there and made Chromium fail to open its password databases in a new data folder, so macOS is unchanged. CEF's `CefSettings::cache_path` cannot move that profile (CEF ignores a child folder under the Chrome bootstrap).
- 2026-10-29 (implementation): the Windows app is CEF's sandboxed layout: `brouk.exe` is CEF's prebuilt `bootstrap.exe`, which starts the sandbox and calls `RunWinMain` in `brouk.dll` (all of Brouk, for every process type). The first window has back, forward, reload and the address bar around one tab; links that would open a tab open a window until the tab strip exists. The smoke test (019) runs on the Windows CI runner: the test pages load, a crashed renderer recovers after a reload, and the app quits when its window is closed.
- 2026-10-29 (implementation): tabs in the title bar. Brouk removes the system title bar (`WM_NCCALCSIZE`) and draws the tab strip and the window's minimise, maximise and close buttons itself; it answers `WM_NCHITTEST` with the system's codes, so dragging, double-click to maximise, Aero Snap, the window menu and Windows 11's snap layouts (on the maximise button) work as in other apps. Tab order, the active tab and where new tabs go come from the core's `TabStripModel`, widths from `LayoutTabStrip`, as on macOS. Popups open as tabs next to their opener, or in a popup window (system title bar, read-only address bar) for `window.open()` with a size, as in Chrome. Colours follow the system's light or dark app mode. Not yet: the tab strip is not exposed to screen readers (UI Automation), and there are no tab tooltips or tab context menu.
- 2026-10-29 (user's decision): the commands of the macOS menu bar are in a "⋮" menu at the right of the toolbar, as in Chrome and Edge on Windows (a classic menu bar and shortcuts only were the alternatives). It grows with the ported features; the macOS app is unchanged. First items: New tab, New window, Brouk Help (F1), Open-source licences, Exit.
- 2026-10-29 (implementation): internal pages. The web pages and the licences (023) are built into `<app>\web`; `brouk://` pages load in their own in-memory request context, as on macOS. The licences open in a tab, help in its own window (016). Pages that need features not yet ported (settings, history, bookmarks, passwords, downloads, profiles) come with those features.
