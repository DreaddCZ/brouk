# 004 – Configuration system

- **Status:** in-progress
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
- 2026-10-21 (implementation, 001): Chromium's own data for a profile lives in `Brouk/web/<profile-id>/`, not in `profiles/<profile-id>/` (Chromium requires profile folders to be direct children of its data root). Our files, including `settings.yaml`, stay in `profiles/<profile-id>/` as specified.
- YAML 1.1 parsers read unquoted `no`, `on`, `off` and similar words as booleans; values must be checked against a typed schema of all settings so such mix-ups are reported as errors (fits the "invalid values are reported" rule).
- 2026-10-22 (implementation): YAML is read and written with yaml-cpp 0.8.0 (MIT, approved by the user). It throws exceptions, so only `src/core/yaml_io.cc` uses it, compiled with exceptions, and turns every error into a returned message; files over 1 MB, nested over 32 levels or with over 100,000 values (alias bombs) are refused. All settings are declared in `src/core/settings_schema.cc` with type, default and limits; booleans accept only `true`/`false`. Unknown keys are reported as problems ("unknown setting"). Files are written atomically (temporary file, then rename) and readable by the user only. Built first: the parts profiles need (files, typed settings, live reload); the settings page later.
- 2026-10-22 (implementation): live reload watches the data folder with FSEvents (Chromium's `web/` folder is ignored) and re-reads settings 0.3 s after a `settings.yaml` changes. Until the settings page exists, problems in the files are written to the log (Console.app, process `brouk`).
- 2026-10-22 (implementation): the settings page is its own window (Brouk > Settings…, Cmd+,), the internal page `brouk://settings`. It is built from the settings list in `src/core/settings_schema.cc`: every setting not marked advanced appears automatically, grouped in sections, with "For all profiles" (global file) and "For one profile" (profile picker, the profile's file) kept apart. Settings needing a restart are labelled ("Needs a restart"; after a change "Restart Brouk to apply"); none need one yet. Invalid values typed on the page are refused with the reason under the setting; invalid values in the files are listed at the top of the page, and a yellow warning in every window's address bar leads there. "Show in Finder" opens the settings files (advanced settings), "Export Settings…/Import Settings…" move the global settings as a `.brouk-settings` file (profiles have their own export, 005).
- 2026-10-22 (implementation): first settings on the page: theme (system/light/dark), search engine (Google, Bing, DuckDuckGo, Startpage, Ecosia or a custom URL with %s), download folder, ask where to save each download, spell checking, default profile, show the profile manager at startup, and each profile's name and colour. A default zoom level (012 configuration) is not offered yet: CEF has no API for Chromium's default zoom.
- 2026-10-26 (settings page layout, user's choice): the page lists its topics on the left (the settings' sections, then "Settings files") and shows one topic on the right, as Chrome's settings do; the window opens 980 × 720. A topic shows its settings for all profiles first and then, under "For each profile", the same topic's per-profile settings with a tab per profile (e.g. Ad blocking: the shared lists, then each profile's kinds of lists). The profile's name and colour are in the Profiles topic. The command palette and links still open the page at a setting: its topic and profile tab are chosen. Narrow windows put the topics above the settings.

