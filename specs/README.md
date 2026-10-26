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
| 007 | [Ad and tracker blocking](007-adblock.md) | done — routing hosts after 010 | P1 |
| 008 | [Password manager](008-password-manager.md) | in-progress — built-in source (forms, saving and filling, generator, 2FA codes and QR reading, passwords page, checkup, import/export, encrypted profile export) in review; Bitwarden next | P1 |
| 009 | [Form autofill](009-autofill.md) | in-progress — entries, custom fields, filling, saving without duplicates, validation, Settings → Autofill with duplicates and JSON export/import in review | P1 |
| 010 | [Link routing and window picker](010-link-routing.md) | in-progress — rules, picker with window thumbnails, wrappers (built-in, own, opaque with "Checking link"), default browser, Settings → Links in review; real Outlook/Teams links to check | P1 |
| 011 | [Bookmark manager](011-bookmarks.md) | done | P1 |
| 012 | [Web platform basics](012-web-platform-basics.md) | in-progress | P1 |
| 013 | [History](013-history.md) | in-progress — storage, recording, retention, address bar suggestions, History menu with closed tabs, history limits, the history page as a tab with search, SQL editor, Clean up and Clear browsing data (merged); window choice of 010 (merged); favicons next; history in profile export moved to 022 | P1 |
| 014 | [Downloads](014-downloads.md) | in-progress — folders per profile, button and popup, downloads page, Dock, quit question, automatic downloads in review | P2 |
| 015 | [Session restore](015-session-restore.md) | done | P1 |
| 016 | [Help](016-help.md) | ready | P2 |
| 017 | [VPN per profile](017-profile-vpn.md) | draft | P2 |
| 018 | [Feedback and issue reporting](018-feedback.md) | draft | P2 |
| 019 | [Release pipeline and automatic updates](019-release-pipeline.md) | ready | P2 |
| 020 | [Task manager](020-task-manager.md) | ready | P2 |
| 021 | [File viewers for text formats](021-file-viewers.md) | idea | P3 |
| 022 | [History in profile export](022-history-export.md) | ready | P3 |

## Roadmap

Build order. Each milestone ends with a build the user tests on their Macs.

| Milestone | Goal | Specs |
|---|---|---|
| **M1 – Browse the web** | A working app that displays web sites correctly: one window, simple tabs, address bar with search, back/forward/reload, standard shortcuts, and all basics from 012 (dialogs, permissions, sign-ins, errors, find, zoom, print, full screen, downloads to `~/Downloads`). | 001 (phase 1 items), 002 (macOS), 012, minimal subset of 003 |
| **M2 – Core model and settings** | Shared window/tab model in the core; YAML settings with live reload and a basic settings page. | 004 |
| **M3 – Profiles** | Isolated profiles, profile groups in one window, profile manager, export/import (parts available so far). | 005 |
| **M4 – Full browser UI** | Tab overflow, pinned tabs, command palette, remappable shortcuts, themes, address bar suggestions from history. | 003, history storage needed by 003 (013) |
| **M5 – Developer tools** | Dock, per-tab toggle, own tools, request rules, API export, remote debugging. | 006 |
| **Later** | In an order to be agreed: | 010, 007, 008, 009, 011, 013, 014, 017 (after checking company computers), 018 and 019 (at the start of phase 2, before the first beta testers), 021 (later phase), then 016 last |

2026-10-22 (user's decision): the rest of 012 (folder upload and file drop, pinch zoom, Picture-in-Picture, permissions in settings) is postponed; profiles (005) come next, on top of the configuration foundation from 004 that they need (settings files, typed settings, live reload). The settings page of 004 follows later.

2026-10-23 (user's decision): next come 003 (M4), then 013, then 011. Full acceptance testing of 004 and 005 is done later, together with the UI of these specs.

2026-10-23 (user's decision): next come bookmarks (011), then ad blocking (007), so the browser can be used day to day; history in profile export (013) is postponed.

2026-10-23 (user's decision): help (016) is built last, after all other specs; until then, rule 7 in `CLAUDE.md` keeps the help text for each change in its pull request.

2026-10-24 (user's decision): VPN per profile (017) goes to the later part of the roadmap; first the user checks what is possible on company computers (admin rights, installed tools).

2026-10-25 (user's decision): feedback and issue reporting (018) is for the first beta testers, so it is built at the start of phase 2, for macOS and later Windows.

2026-10-25 (user's decision): the release pipeline (019) is built together with 018 at the start of phase 2.

2026-10-26 (user's decision): 019 rewritten: releases are built, notarized and smoke-tested on the user's Intel Mac (no GitHub Actions minutes), published in a public releases repository, and installed copies update themselves with Sparkle (stable/beta channel).

2026-10-26 (user's decision): history in profile export moved from 013 and 005 to its own spec, 022, kept for later; 013 is finished without it. Next: the window choice of 010 for the bookmark and history pages, then favicons (013), then acceptance testing of 005, 013 and 014.

2026-10-25 (user's decision): file viewers for text formats (021) are recorded at a high level for a later phase.

Rules: a milestone uses only `ready` specs; later milestones may be re-ordered by the user. Unspecified standard browser behaviour follows Chrome (see `CLAUDE.md`).
