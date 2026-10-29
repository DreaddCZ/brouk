# 024 – Windows app

- **Status:** in-progress
- **Priority:** P2
- **Depends on:** 001, 002, and every spec it ports (003–016, 019, 020, 022, 023)

## Goal

Brouk on Windows 10 and 11 (x64) with the same features as on macOS, so the user and colleagues on Windows PCs can use it for work (002).

## Description

The Windows app is the existing portable core (`src/core/`, `src/cef/`, `src/renderer/`), the same internal pages (`web/`), and a new native shell in C++ on the Win32 API in `src/platform/win/` (decided in 001). The shell does what `src/platform/mac/` does on macOS: windows, the tab strip, toolbar and address bar, menus, popups and prompts, and the system integration (secret store, default browser, updates). Features behave as their specs describe; where a spec names a macOS control or shortcut, the Windows app uses the Windows equivalent (Ctrl for ⌘, the window's menu for the app menu), as Chrome does on Windows.

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
