# 004 – Configuration system

- **Status:** ready
- **Priority:** P1
- **Depends on:** 001

## Goal

Make the browser "fully configurable": every behaviour and UI option is adjustable, and settings are easy to back up, share and version.

## Description

- A settings UI for common options.
- A human-readable YAML config file (per profile) exposing *all* options, including advanced ones not shown in the UI — similar to Firefox's `about:config`, but file-based.
- Changes in the file apply without manual migration; invalid values are reported, not silently ignored.
- **Live reload:** changes made in the settings UI or in the file take effect while the browser is running. Settings that need a restart are marked as such in the settings UI, and when one of them changes, the user is told that a restart is needed.
- Global settings (app-wide) and per-profile settings are clearly separated.
- **Location (macOS):** global settings in `~/Library/Application Support/Brouk/settings.yaml`; each profile's settings in its own storage directory, `~/Library/Application Support/Brouk/profiles/<profile-id>/settings.yaml`, where `<profile-id>` is a stable ID that does not change when the profile is renamed.
- The files are machine-written: when the settings UI changes a value, the file is rewritten, and comments or key order the user added are not kept. Editing values by hand is supported.
- Settings can be exported and imported manually (this is the way to move settings between devices for now).

## Acceptance criteria

- [ ] Every setting in the UI is also in the config file, and vice versa for non-advanced settings.
- [ ] Config files are YAML.
- [ ] Editing the config file while the browser runs applies the change without a restart, except for settings marked as requiring a restart.
- [ ] Settings that require a restart are marked in the settings UI, and changing one tells the user a restart is needed.
- [ ] An invalid config value produces a clear error message and falls back to the default.
- [ ] Settings can be exported to a file and imported on another machine.
- [ ] Each profile has its own settings; global settings apply to all profiles.

## Out of scope

- Settings sync across devices — wanted eventually, as a later spec; manual export/import covers it for now.
- Managed/enterprise policies (admin-locked settings) — not in the first versions, possibly later.
- Preserving comments and key order in config files.

## Configuration

This spec defines the configuration system itself.

## Open questions

None. (Windows location to be decided together with Windows support, see 002.)

## Notes

- 2026-10-21 (settings review): YAML format, live reload with restart-required settings marked, no managed policies in the first versions, sync eventually but manual export/import for now. Files live in Application Support (global + per-profile directory); comments in the files are not preserved. Spec set to `ready`.
- 2026-10-21 (readiness review): profile folders are named by a stable profile ID, not the profile name, so renaming a profile never moves its data. The data folder name `Brouk` comes from the app name (decided 2026-10-21, see "Decisions" in `CLAUDE.md`; kept in one CMake variable).
- YAML 1.1 parsers read unquoted `no`, `on`, `off` and similar words as booleans; values must be checked against a typed schema of all settings so such mix-ups are reported as errors (fits the "invalid values are reported" rule).
