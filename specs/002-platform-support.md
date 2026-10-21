# 002 – Platform support

- **Status:** draft
- **Priority:** P1
- **Depends on:** 001

## Goal

Ship on the platform the user works on first, then expand, without design choices that block later platforms.

## Description

Phased rollout:

1. **macOS** – first and primary target.
2. **Windows** – second.
3. **Mobile** – later (Android and/or iOS).

Code and architecture should keep platform-specific parts isolated so Windows can be added without a rewrite.

**Mobile caveat:** Android can run Chromium. On iOS, Apple generally requires browsers to use WebKit; alternative engines like Chromium are allowed only in the EU under the Digital Markets Act, with extra Apple requirements. An iOS version may therefore need a different engine or be EU-only — this needs research before mobile work starts.

## Acceptance criteria

- [ ] macOS: runs natively on Apple Silicon; signed and notarized so it opens without security warnings.
- [ ] Windows: builds and runs on Windows 10/11 x64 using the same feature set (phase 2).
- [ ] Platform-specific code is kept in clearly separated modules.
- [ ] User data (profiles, settings) uses each OS's standard app data location.

## Out of scope

- Mobile implementation (separate spec later).

## Configuration

n/a

## Open questions

- Minimum macOS version? Support Intel Macs or Apple Silicon only?
- Linux: wanted at all? If yes, which phase?
- Windows on ARM?
- Distribution: direct download, Homebrew, Mac App Store, winget?
- Auto-updates: needed from the first release?

## Notes
