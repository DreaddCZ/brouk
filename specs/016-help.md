# 016 – Help

- **Status:** draft
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
- **Context links from other pages:** a small, consistent "?" button next to features that need explaining opens help at that feature's section. At least:

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

- Written in **English**, short, task-oriented ("how do I…"), with examples the user can copy (search filters, SQL queries, URL patterns, rules).
- **Help is part of each feature's definition of done:** a spec that adds or changes user-visible behaviour also adds or updates its help section and its "?" links. Specs that are already done get their sections as part of this spec.

## Acceptance criteria

- [ ] Help → Brouk Help (⇧⌘?) opens `brouk://help` in its own window; opening it again reuses the same window and brings it to the front.
- [ ] The help window remembers its size and position after closing and after restart.
- [ ] The page works with no network connection.
- [ ] The table of contents lists all sections and highlights the one in view; clicking an entry scrolls to it.
- [ ] The search box filters sections by their text, ignoring case and accents, and highlights the matches.
- [ ] Every "?" link listed above opens the help window scrolled to its section, with the section briefly highlighted.
- [ ] A build-time test checks that every help link in the code and pages points to an anchor that exists in the help page; a missing anchor fails the build.
- [ ] The Keyboard shortcuts section shows the user's current shortcuts, including remapped ones; changing a shortcut updates it (live reload, 004).
- [ ] The Settings reference lists every non-advanced setting from the schema with key, default and scope.
- [ ] The command palette finds help sections by title and opens them.
- [ ] Web links inside help open as a tab of the default profile.
- [ ] The page follows the light/dark theme of the other internal pages.
- [ ] Sections exist for every feature that is `done` or `in-progress` when this spec is implemented.

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

1. **Own window (recommended) or a tab?** The description assumes its own window for the reasons above. Alternative: a tab in the active tab's profile group, like Chrome's help.
2. **Format of the help content:** written as Markdown files in the repo and converted to HTML at build time (needs one pinned build-time library, e.g. `markdown-it`, which needs approval under `CLAUDE.md`), or written directly as HTML (no new dependency, harder to edit)?
3. **"?" link style:** a "?" icon button next to the feature (proposed), or a text link "Help", or both?
4. Should **CLAUDE.md** get a rule that every user-visible change updates help (as in Description → Content), so Claude Code does it without being reminded?

## Notes

- 2026-10-23: created as a high-level requirement; details to be agreed in a separate review.
