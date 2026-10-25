# 002 – Platform support

- **Status:** ready (for macOS; Windows and mobile parts are not in scope yet)
- **Priority:** P1
- **Depends on:** 001

## Goal

Ship on the platform the user works on first, then expand, without design choices that block later platforms.

## Description

Phased rollout:

1. **macOS** – first and primary target, on both Apple Silicon and Intel (two separate builds). Minimum version: **macOS 15 Sequoia**.
2. **Windows** – second.
3. **Mobile** – later (Android and/or iOS).

Code and architecture should keep platform-specific parts isolated so Windows can be added without a rewrite.

**Mobile caveat:** Android can run Chromium. On iOS, Apple generally requires browsers to use WebKit; alternative engines like Chromium are allowed only in the EU under the Digital Markets Act, with extra Apple requirements. An iOS version may therefore need a different engine or be EU-only — this needs research before mobile work starts.

## Acceptance criteria

- [ ] macOS: runs natively on Apple Silicon (arm64) and Intel (x86_64) as two separate builds, on macOS 15 and later (deployment target 15.0). Phase 2 (sharing with colleagues, see 001): each build signed and notarized so it opens without security warnings.
- [ ] Phase 2: each macOS build updates itself from its own update feed; an Intel install never receives an Apple Silicon build and vice versa.
- [ ] Windows (when Windows work starts — not tied to release phase 2): builds and runs on Windows 10/11 x64 using the same feature set.
- [ ] Platform-specific code is kept in clearly separated modules.
- [ ] User data (profiles, settings) uses each OS's standard app data location.

## Out of scope

- Mobile implementation (separate spec later).

## Configuration

n/a

## Open questions

None for macOS. Deferred until they become relevant (they do not block macOS work):

- Linux as a user-facing platform: wanted at all? (The core already builds on Linux for tests, see 001.)
- Windows on ARM?
- Distribution: direct download, Homebrew, Mac App Store, winget?

## Notes

- 2026-10-21 (readiness review): set to `ready` for macOS. "Phase 1/2" in 001 means release phases (personal use / colleagues); Windows and mobile are platform rollout steps and are not tied to those phases. Remaining questions deferred.

- 2026-10-21: minimum macOS is 15 Sequoia. The user's 2019 Intel MacBook runs macOS 15.8, and 15 and 26 are the versions the user can test on. Chromium (and so CEF) requires macOS 13 Ventura or later since Chrome 151 (July 2026); the minimum can be lowered towards that if a colleague needs it in phase 2.

- 2026-10-21: automatic, regular updates are required — the user does not want to maintain the app manually. Mechanism is defined in 001 ("Update process").
- 2026-10-21: Intel Macs are supported because the user works on a 2019 Intel MacBook part of the time. Delivered as two separate builds (not a universal binary) to avoid doubling the download size; CI builds, tests, signs and notarizes both. The user's Intel MacBook serves as the Intel test machine.
- Context: macOS 26 Tahoe is the last macOS for Intel Macs, Apple security updates for it are expected to end around 2029, and Chromium will eventually drop Intel macOS as well. When CEF stops shipping Intel builds, Intel support ends with it.
- 2026-10-25: Windows CI started ahead of Windows work (user's decision): `.github/workflows/windows.yml` builds the portable code (core, CEF integration, renderer, Rust ad blocking library) on Windows x64 with clang-cl and runs the core and internal-page tests, weekly and on demand. Choices: clang-cl rather than MSVC's cl.exe, so the same clang warnings apply on every platform; the static C runtime everywhere (as CEF's own build); sanitizers only on Linux; clang-cl is the runner's LLVM (20.1.8 at the start), not pinned, like clang on the Linux runner. First run: all 395 core tests and 108 internal-page tests pass, about 16 minutes. Windows-specific gaps found: POSIX permission bits (a settings-file test skips that check on Windows) and crash signal names (Windows uses exception codes; to be named when the Windows app exists). The Windows UI toolkit is still open (001).
