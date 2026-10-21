# 004 – Configuration system

- **Status:** draft
- **Priority:** P1
- **Depends on:** 001

## Goal

Make the browser "fully configurable": every behaviour and UI option is adjustable, and settings are easy to back up, share and version.

## Description

- A settings UI for common options.
- A human-readable config file (per profile) exposing *all* options, including advanced ones not shown in the UI — similar to Firefox's `about:config`, but file-based.
- Changes in the file apply without manual migration; invalid values are reported, not silently ignored.
- Global settings (app-wide) and per-profile settings are clearly separated.
- Settings can be exported and imported.

## Acceptance criteria

- [ ] Every setting in the UI is also in the config file, and vice versa for non-advanced settings.
- [ ] Editing the config file and restarting (or reloading) applies the change.
- [ ] An invalid config value produces a clear error message and falls back to the default.
- [ ] Settings can be exported to a file and imported on another machine.
- [ ] Each profile has its own settings; global settings apply to all profiles.

## Out of scope

- Settings sync across devices (possible later spec).

## Configuration

This spec defines the configuration system itself.

## Open questions

- File format: TOML, JSON, YAML?
- Live reload of the config file, or apply on restart?
- Managed/enterprise policies (admin-locked settings)?
- Should settings sync between devices eventually?

## Notes
