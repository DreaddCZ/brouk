# 016 – Help

- **Status:** done
- **Priority:** P2
- **Depends on:** 001, 003, 004

## Goal

Brouk has features that are not obvious from the UI alone: history search filters and SQL (013), request rules, token decoder and API export (006), profiles and link routing rules (005, 010), URL patterns, settings files and shortcuts (004, 003). The user and later colleagues need one place that explains them, reachable directly from the place where the feature is used, so they do not have to search the specs or ask.

## Description

### Help page

- An internal HTML page, `brouk://help`, bundled with the app. It works **offline** and always describes the installed version.
- One page with sections, one per feature area (for example: Profiles, Link routing, History, History search, Developer tools, Request rules, Bookmarks, Downloads, Settings files, Keyboard shortcuts, URL patterns).
- Every section and subsection has a **stable anchor**, e.g. `brouk://help/#history-search`, `brouk://help/#devtools-request-rules`. Anchors are part of the contract: links from other pages depend on them, so renaming one needs updating every link (checked by a test, see below).
- Layout:
  - a table of contents on the left, which highlights the section in view;
  - a **search box** that filters the sections and highlights matches (ignoring case and accents, like the command palette);
  - follows the internal pages' theme (light/dark).
- Generated sections stay in sync with the code instead of being written by hand:
  - **Keyboard shortcuts** — listed from the shortcuts list (`src/core/shortcuts.cc`) with the user's **current** values, not only the defaults;
  - **Settings reference** — every setting from the settings schema (`src/core/settings_schema.cc`) with its key, default, scope (global / per profile) and description.

### Where it opens: its own window

Help opens in **its own window**, like settings and the profile manager — not as a tab. Reasons:

- Tabs belong to a profile group (005), but internal pages have no profile; a help tab would have to be put into some profile's group artificially.
- Help is read **next to** the page it explains — e.g. the help window beside the history page while typing search filters. A tab would hide the page the user is working on.
- It keeps the tab strip free of non-web tabs.

Rules:

- There is **one** help window. Opening help again brings it to the front and scrolls to the requested section instead of opening another window.
- The window remembers its size and position.
- Links to web sites inside help open as a tab of the default profile (as links in DevTools windows of internal pages, 006).

### Opening help

- **Help menu** in the menu bar (standard on macOS): "Brouk Help" (⇧⌘? — remappable as `shortcuts.help`), "Keyboard Shortcuts" (opens the shortcuts section).
- **Command palette** (⌘K): finds help sections by title, e.g. "help request rules" or just "request rules" lists the help section next to the setting and action.
- **Context links from other pages:** a small, consistent **icon button with a question mark** next to features that need explaining opens help at that feature's section. At least:

| Where | Opens |
|---|---|
| History page – search box and filter chips (013) | `#history-search` |
| History page – SQL editor (013) | `#history-sql` |
| History – Clean up dialog (013) | `#history-clean-up` |
| Developer tools dock – each own tool (site data, tokens, JSON viewer, requests, rules, API export) (006) | `#devtools-<tool>` |
| Profile manager (005) | `#profiles` |
| Link routing rules (010) | `#link-routing` |
| Settings page – each section header, and URL pattern fields | `#settings-<section>`, `#url-patterns` |
| Bookmark manager (011), downloads (014), password manager (008), autofill (009), adblock (007) — when they are built | their section |

- A context link opens the section **scrolled into view and briefly highlighted**, so the user sees where to read.
- Native UI (toolbar popovers, dialogs) can link to help the same way.

### Content

- Written as **Markdown** files in the repo (`web/help/content/`, one file per section), converted to the HTML page at build time with **markdown-it** (MIT, pinned version, build-time only — nothing of it ships in the app). Anchors are set explicitly in the Markdown, not derived from headings, so rewording a heading does not break links.
- Written in **English**, short, task-oriented ("how do I…"), with examples the user can copy (search filters, SQL queries, URL patterns, rules).
- **Help is part of each feature's definition of done:** a spec that adds or changes user-visible behaviour also adds or updates its help section and its "?" links. Specs that are already done get their sections as part of this spec.

## Acceptance criteria

- [x] Help → Brouk Help (⇧⌘?) opens `brouk://help` in its own window; opening it again reuses the same window and brings it to the front.
- [x] The help window remembers its size and position after closing and after restart.
- [x] The page works with no network connection.
- [x] The table of contents lists all sections and highlights the one in view; clicking an entry scrolls to it.
- [x] The search box filters sections by their text, ignoring case and accents, and highlights the matches.
- [x] Every "?" icon button listed above opens the help window scrolled to its section, with the section briefly highlighted.
- [x] A build-time test checks that every help link in the code and pages points to an anchor that exists in the help page; a missing anchor fails the build.
- [x] The Keyboard shortcuts section shows the user's current shortcuts, including remapped ones; changing a shortcut updates it (live reload, 004).
- [x] The Settings reference lists every non-advanced setting from the schema with key, default and scope.
- [x] The command palette finds help sections by title and opens them.
- [x] Web links inside help open as a tab of the default profile.
- [x] The page follows the light/dark theme of the other internal pages.
- [x] Sections exist for every feature that is `done` or `in-progress` when this spec is implemented.

## Out of scope

- Online help, a help web site, or downloading help content.
- Translations (English only for now).
- Videos, interactive tours, onboarding walkthroughs or tooltips that pop up on first use.
- A "What's new" page or release notes (may come with automatic updates in phase 2, 001).
- macOS Help Book integration (Apple Help Viewer); the Help menu's built-in menu search still works as usual.

## Configuration

| Setting | Default | Scope |
|---|---|---|
| `shortcuts.help` | `shift+cmd+?` | global |

## Open questions

None.

## Notes

- 2026-10-23: created as a high-level requirement; details to be agreed in a separate review.
- 2026-10-23 (help review with the user): help opens in its own window (not a tab); content is written in Markdown and converted at build time with markdown-it (approved as a pinned, build-time-only dependency); context links are an icon button with a question mark; CLAUDE.md gets a rule that every user-visible change updates help. Spec set to `ready`.
- 2026-10-27 (implementation): `brouk://help` is built by `web/help/build.mjs` from `web/help/content/*.md` (file order = section order; `## Title {#anchor}` headings, the anchor written in the Markdown) into the bundled page, with `sections.json` for the command palette. The same script checks every help link — `helpButton('…')`, `openHelp('…')`, `data-help="…"`, `BRHelpButton(@"…")`, `openHelpAt:@"…"`, `brouk://help/#…` and links between sections — against the anchors and fails the build on a missing one; the settings reference's anchors (`settings-<section>`) come from the settings schema. Any internal page opens help with the bridge message `help.open` (`src/cef/help_link.cc`, handled before the page's own handler; anchors are checked as lowercase letters, digits and `-`). One help window (`Help → Brouk Help`, the action is `showBroukHelp:` because NSApplication answers `showHelp:` itself with "Help isn't available"); opening it again runs `showHelpSection()` in the page, which scrolls and highlights even when the section is already shown; its frame is remembered by macOS (`BroukHelpWindow`). The keyboard shortcuts and the settings reference come from the browser (`help.state`) and are refreshed when the window gets focus, so a remapped shortcut shows after switching back. Search hides sections that do not contain every word (case and accents ignored) and marks the matches. Web links on the help page (also ⌘-clicked) open as a tab of the default profile; other navigation stays blocked. "?" buttons: history page (page, search, SQL, Clean up, Clear browsing data), bookmark manager, downloads page, passwords page and checkup, profile manager and its links field, every settings topic and URL pattern field, Settings → Profiles' links, the developer tools page (per tool) and the Postman export, the link picker and the shield popup (native macOS help buttons). The JSON viewer runs inside web pages without the bridge, so it has no "?" of its own; its section is reachable from the developer tools and the palette.
- 2026-10-27 (implementation, open point for the user): macOS reserves ⇧⌘/ (⇧⌘?) for the Help menu's search box in every app, so the default shortcut of Brouk Help opens that search box (where typing "help" finds Brouk Help) rather than the window. The default stays as specified until the user decides; `shortcuts.help` can be set to another shortcut.
