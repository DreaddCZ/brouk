# 002 – Platform support

- **Status:** draft
- **Priority:** P1
- **Depends on:** 001

## Goal

Ship on the platform the user works on first, then expand, without design choices that block later platforms.

## Description

Phased rollout:

1. **macOS** – first and primary target, on both Apple Silicon and Intel (two separate builds).
2. **Windows** – second.
3. **Mobile** – later (Android and/or iOS).

Code and architecture should keep platform-specific parts isolated so Windows can be added without a rewrite.

**Mobile caveat:** Android can run Chromium. On iOS, Apple generally requires browsers to use WebKit; alternative engines like Chromium are allowed only in the EU under the Digital Markets Act, with extra Apple requirements. An iOS version may therefore need a different engine or be EU-only — this needs research before mobile work starts.

## Acceptance criteria

- [ ] macOS: runs natively on Apple Silicon (arm64) and Intel (x86_64) as two separate builds, each signed and notarized so it opens without security warnings.
- [ ] Each macOS build updates itself from its own update feed; an Intel install never receives an Apple Silicon build and vice versa.
- [ ] Windows: builds and runs on Windows 10/11 x64 using the same feature set (phase 2).
- [ ] Platform-specific code is kept in clearly separated modules.
- [ ] User data (profiles, settings) uses each OS's standard app data location.

## Out of scope

- Mobile implementation (separate spec later).

## Configuration

n/a

## Open questions

- Minimum macOS version? It must be one the user's 2019 Intel MacBook can run (check its latest supported macOS), and one that current CEF/Chromium still supports.
- Linux: wanted at all? If yes, which phase?
- Windows on ARM?
- Distribution: direct download, Homebrew, Mac App Store, winget?

## Notes

- 2026-10-21: automatic, regular updates are required — the user does not want to maintain the app manually. Mechanism is defined in 001 ("Update process").
- 2026-10-21: Intel Macs are supported because the user works on a 2019 Intel MacBook part of the time. Delivered as two separate builds (not a universal binary) to avoid doubling the download size; CI builds, tests, signs and notarizes both. The user's Intel MacBook serves as the Intel test machine.
- Context: macOS 26 Tahoe is the last macOS for Intel Macs, Apple security updates for it are expected to end around 2029, and Chromium will eventually drop Intel macOS as well. When CEF stops shipping Intel builds, Intel support ends with it.
