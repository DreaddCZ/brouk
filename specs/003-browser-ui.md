# 003 – Browser UI

- **Status:** ready
- **Priority:** P1
- **Depends on:** 001, 004, 005

## Goal

A clean, minimal browser interface built for focused work, which the user can rearrange and control from the keyboard.

## Description

Core UI elements:

- Windows with tabs, an address/search bar, back/forward/reload.
- **Address bar search:** text that is not a URL is searched with the default search engine — **Google** by default. The search engine is configurable: choose from a built-in list (Google, Bing, DuckDuckGo, Startpage, Ecosia) or add a custom one with a URL template (`https://example.com/search?q=%s`).
- **Address bar suggestions,** in this order: (1) **history** of the active tab's profile, (2) **bookmarks** of that profile, (3) **search suggestions** from the search engine, last. Each source can be turned off; search suggestions send what the user types to the search engine (as in Chrome) and are on by default.
- Minimal chrome by default: no promotional content, news feed or unnecessary buttons.
- Keyboard-first: every action reachable by a shortcut; shortcuts remappable.
- A command palette (search for any action, tab, bookmark or setting).
- Horizontal tab strip. Vertical tabs come later (see Out of scope).
- Tab groups: each profile's tabs in a window form one group, labelled with the profile's name, colour and number of open tabs (see 005). Clicking the group label collapses or expands the group, as in Chrome. Each group has its own "+" button that opens a new tab in that group.
- The default profile's group is always present in every window, even with no tabs, at the far right of the tab strip (at the bottom with vertical tabs later). Other profiles' groups appear when they get their first tab and disappear when their last tab closes.
- Pinned tabs work as in Chrome, with these differences: a pinned tab sits at the start of its own profile group (not outside the groups), it keeps its close button, and it stays wider than other tabs when space is short — e.g. icon plus the first few letters of its title while other tabs are icon-only.
- When tabs do not fit:
  1. They shrink down to just their icon.
  2. If there is still not enough space, other groups collapse automatically to make room for the group the user is working in.
  3. If one group alone still does not fit, it shows the current tab, its pinned tabs and the 3 tabs after the current one, and collapses the rest. This step is an edge case; details will be tuned during testing.
- The close button (✕) stays visible on every tab at every width and is visually separated from the rest of the tab, so the user does not close a tab by accident when trying to drag it.
- Light and dark theme following the OS by default.
- Clear indication of the profile: every tab shows its profile group, and the toolbar shows the active tab's profile colour.
- Bookmarks, history and downloads views — specified separately in 011, 013 and 014 (later).

## Acceptance criteria

- [ ] Basic browsing: open, close, reorder and switch tabs; navigate; reload; back/forward.
- [ ] All toolbar actions have a keyboard shortcut, and shortcuts can be changed in settings.
- [ ] Command palette opens with a shortcut and can run actions and switch tabs.
- [ ] Tabs are shown in a horizontal strip.
- [ ] When tabs do not fit, they shrink down to their icon; if that is not enough, other groups collapse to make room for the group in use.
- [ ] Every tab's close button is visible at every tab width and visually separated from the tab's drag area.
- [ ] A tab group can be collapsed and expanded by clicking its label; the label shows the number of open tabs.
- [ ] Every group has a "+" button that opens a new tab in that group's profile.
- [ ] Every window always shows the default profile's group at the far right of the tab strip, also when it has no tabs.
- [ ] Tabs can be pinned and unpinned; a pinned tab sits at the start of its profile group and shows a close button.
- [ ] When tabs shrink, pinned tabs stay wider than other tabs while space allows (icon plus the start of the title vs. icon only).
- [ ] Every tab shows which profile it belongs to (its group's name and colour), and the toolbar shows the active tab's profile colour.
- [ ] Theme follows the OS light/dark setting, with manual override.
- [ ] Typing text that is not a URL searches with the default search engine (Google unless changed); the engine can be changed in settings, including a custom URL template.
- [ ] Address bar suggestions list history matches first, then bookmark matches (after 011), then search suggestions; each source can be turned off.

## Out of scope

- Detailed visual design (separate design work).
- Vertical tabs — planned for later, in a separate spec.
- Workspaces (named, switchable sets of tabs) — not planned for now; profile groups cover the need.

## Configuration

Theme, toolbar items, shortcuts, new tab page content, default search engine (Google) and custom search engines, suggestion sources (history, bookmarks, search suggestions — all on).

## Open questions

None.

## Notes

- 2026-10-21 (readiness review): default search engine Google, configurable; suggestions from history first, then bookmarks, search suggestions last. Bookmarks (011), history (013) and downloads (014) views are specified later. Search engine and suggestion settings are global for now (provisional — could become per profile later).
- 2026-10-21 (readiness review): milestone 1 ("browse the web", see roadmap) needs only a minimal subset of this spec: one window with a simple tab strip (no profile groups yet), address bar with search, back/forward/reload, and the standard shortcuts. Build that subset so it grows into the full UI here — no throwaway shell.
- 2026-10-21 (UI and profiles review): left to be resolved during testing, not specified now: the exact behaviour in step 3 of tab overflow, split view, new tab page content, session restore on startup, reopening closed tabs.
- 2026-10-21 (UI and profiles review): pinned tabs sit at the start of their profile group, keep the close button and stay wider than other tabs when space is short. Spec set to `ready`; implementation also needs the configuration decision in 004 (shortcuts, theme and other settings).

- 2026-10-21 (decision in 001): hybrid UI shell. Windows, tabs, toolbar, address bar and menus are native (Objective-C++/AppKit on macOS); settings, history, bookmarks, downloads and other internal pages are HTML/TypeScript pages rendered in CEF. This answers "native look or custom cross-platform look" for the main window chrome; internal pages follow the same theme.
- 2026-10-21 (UI and profiles review): horizontal tabs first, vertical later. Tab groups are profile groups (one group per profile per window, see 005), collapsible like in Chrome. Separate workspaces are not needed for now.
