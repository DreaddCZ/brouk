# 005 – Profiles

- **Status:** draft
- **Priority:** P1
- **Depends on:** 001, 004

## Goal

Keep separate work contexts (e.g. different clients, employers, personal) fully isolated, like Firefox profiles.

## Description

A profile is a completely separate set of browser data: cookies and logins, history, bookmarks, saved passwords, autofill data, extensions, settings and cache.

- Create, rename, delete and pick a colour/icon for a profile.
- A profile manager to choose which profile to open (optionally shown at startup).
- Several profiles can run at the same time, each in its own windows.
- A profile can be launched directly, e.g. from the command line (`--profile <name>`) or a dock/desktop shortcut.
- Nothing leaks between profiles.

## Acceptance criteria

- [ ] User can create, rename and delete profiles, and set a name and colour.
- [ ] Logging into a site in profile A does not log in profile B.
- [ ] History, bookmarks, passwords, autofill data, extensions and settings are separate per profile.
- [ ] Two or more profiles can be open simultaneously.
- [ ] A specific profile can be started from the command line.
- [ ] The profile manager can be set to appear at startup or be skipped (opening a default profile).
- [ ] Deleting a profile removes all of its data, after confirmation.

## Out of scope

- Sync of profiles between devices.

## Configuration

Default profile, show profile manager at startup (yes/no), per-profile name, colour and icon.

## Open questions

- Also add Firefox-style *containers* (isolated tabs within one profile)?
- Open links from other apps in which profile? (Ask each time, default profile, rules per domain?)
- Import profiles/data from Chrome, Firefox or Safari?
- Temporary (throwaway) profiles?

## Notes

- 2026-10-21 (decision in 001): profiles are our own implementation, not Chrome's profile system. Each profile is a separate CEF request context with its own storage directory. Because isolation is per request context rather than per window, profiles and containers are not tied to windows the way they are in Chrome.
