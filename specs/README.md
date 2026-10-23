# Specs index

Every feature or requirement has its own file in this folder, based on [`_template.md`](_template.md).

## How to add a spec

1. Copy `_template.md` to `NNN-short-name.md`, using the next free number (e.g. `001-tab-groups.md`).
2. Fill in at least the goal and a short description; set status to `idea` or `draft`.
3. Add a row to the table below.
4. When all acceptance criteria are written and open questions are resolved, set status to `ready`.

## Statuses

| Status | Meaning |
|---|---|
| `idea` | Rough note, not ready for work |
| `draft` | Being specified, may have open questions |
| `ready` | Fully specified, can be implemented |
| `in-progress` | Being implemented |
| `done` | Implemented and verified |

Priority: `P1` (must have) · `P2` (should have) · `P3` (nice to have)

## Specs

| ID | Title | Status | Priority |
|---|---|---|---|
| 001 | [Technology stack](001-technology-stack.md) | in-progress | P1 |
| 002 | [Platform support](002-platform-support.md) | ready (macOS) | P1 |
| 003 | [Browser UI](003-browser-ui.md) | done — bookmark suggestions follow with 011 | P1 |
| 004 | [Configuration system](004-configuration.md) | in-progress | P1 |
| 005 | [Profiles](005-profiles.md) | in-progress | P1 |
| 006 | [Developer tools](006-developer-tools.md) | in-progress | P1 |
| 007 | [Ad and tracker blocking](007-adblock.md) | ready | P1 |
| 008 | [Password manager](008-password-manager.md) | ready | P1 |
| 009 | [Form autofill](009-autofill.md) | ready | P1 |
| 010 | [Link routing and window picker](010-link-routing.md) | ready | P1 |
| 011 | [Bookmark manager](011-bookmarks.md) | ready | P1 |
| 012 | [Web platform basics](012-web-platform-basics.md) | in-progress | P1 |
| 013 | [History](013-history.md) | in-progress — storage, recording, retention, address bar suggestions (merged) and History menu with closed tabs (PR #20) and history limits — forwarding pages, "never record" list (PR #21) built; history page, search, SQL, clean up, clearing and export next | P1 |
| 014 | [Downloads](014-downloads.md) | ready | P2 |

## Roadmap

Build order. Each milestone ends with a build the user tests on their Macs.

| Milestone | Goal | Specs |
|---|---|---|
| **M1 – Browse the web** | A working app that displays web sites correctly: one window, simple tabs, address bar with search, back/forward/reload, standard shortcuts, and all basics from 012 (dialogs, permissions, sign-ins, errors, find, zoom, print, full screen, downloads to `~/Downloads`). | 001 (phase 1 items), 002 (macOS), 012, minimal subset of 003 |
| **M2 – Core model and settings** | Shared window/tab model in the core; YAML settings with live reload and a basic settings page. | 004 |
| **M3 – Profiles** | Isolated profiles, profile groups in one window, profile manager, export/import (parts available so far). | 005 |
| **M4 – Full browser UI** | Tab overflow, pinned tabs, command palette, remappable shortcuts, themes, address bar suggestions from history. | 003, history storage needed by 003 (013) |
| **M5 – Developer tools** | Dock, per-tab toggle, own tools, request rules, API export, remote debugging. | 006 |
| **Later** | In an order to be agreed: | 010, 007, 008, 009, 011, 013, 014 |

2026-10-22 (user's decision): the rest of 012 (folder upload and file drop, pinch zoom, Picture-in-Picture, permissions in settings) is postponed; profiles (005) come next, on top of the configuration foundation from 004 that they need (settings files, typed settings, live reload). The settings page of 004 follows later.

2026-10-23 (user's decision): next come 003 (M4), then 013, then 011. Full acceptance testing of 004 and 005 is done later, together with the UI of these specs.

Rules: a milestone uses only `ready` specs; later milestones may be re-ordered by the user. Unspecified standard browser behaviour follows Chrome (see `CLAUDE.md`).
