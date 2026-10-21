# 003 – Browser UI

- **Status:** draft
- **Priority:** P1
- **Depends on:** 001, 004, 005

## Goal

A clean, minimal browser interface built for focused work, which the user can rearrange and control from the keyboard.

## Description

Core UI elements:

- Windows with tabs, an address/search bar, back/forward/reload.
- Minimal chrome by default: no promotional content, news feed or unnecessary buttons.
- Keyboard-first: every action reachable by a shortcut; shortcuts remappable.
- A command palette (search for any action, tab, bookmark or setting).
- Horizontal tab strip. Vertical tabs come later (see Out of scope).
- Tab groups: each profile's tabs in a window form one group, labelled with the profile's name, colour and number of open tabs (see 005). Clicking the group label collapses or expands the group, as in Chrome. Each group has its own "+" button that opens a new tab in that group.
- The default profile's group is always present in every window, even with no tabs, at the far right of the tab strip (at the bottom with vertical tabs later). Other profiles' groups appear when they get their first tab and disappear when their last tab closes.
- Pinned tabs work as in Chrome.
- When tabs do not fit:
  1. They shrink down to just their icon.
  2. If there is still not enough space, other groups collapse automatically to make room for the group the user is working in.
  3. If one group alone still does not fit, it shows the current tab, its pinned tabs and the 3 tabs after the current one, and collapses the rest. This step is an edge case; details will be tuned during testing.
- The close button (✕) stays visible on every tab at every width and is visually separated from the rest of the tab, so the user does not close a tab by accident when trying to drag it.
- Light and dark theme following the OS by default.
- Clear indication of the profile: every tab shows its profile group, and the toolbar shows the active tab's profile colour.
- Bookmarks, history and downloads views.

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
- [ ] Tabs can be pinned and unpinned.
- [ ] Every tab shows which profile it belongs to (its group's name and colour), and the toolbar shows the active tab's profile colour.
- [ ] Theme follows the OS light/dark setting, with manual override.

## Out of scope

- Detailed visual design (separate design work).
- Vertical tabs — planned for later, in a separate spec.
- Workspaces (named, switchable sets of tabs) — not planned for now; profile groups cover the need.

## Configuration

Theme, toolbar items, shortcuts, new tab page content.

## Open questions

- Pinned tabs and profile groups: does a pinned tab sit at the start of its own profile group, or at the start of the whole tab strip as in Chrome? (Every tab belongs to a profile group, see 005.)
- Pinned tabs and the close button: Chrome hides ✕ on pinned tabs; this spec says ✕ is always visible on every tab. Which wins?

## Notes

- 2026-10-21 (UI and profiles review): left to be resolved during testing, not specified now: the exact behaviour in step 3 of tab overflow, split view, new tab page content, session restore on startup, reopening closed tabs.

- 2026-10-21 (decision in 001): hybrid UI shell. Windows, tabs, toolbar, address bar and menus are native (Objective-C++/AppKit on macOS); settings, history, bookmarks, downloads and other internal pages are HTML/TypeScript pages rendered in CEF. This answers "native look or custom cross-platform look" for the main window chrome; internal pages follow the same theme.
- 2026-10-21 (UI and profiles review): horizontal tabs first, vertical later. Tab groups are profile groups (one group per profile per window, see 005), collapsible like in Chrome. Separate workspaces are not needed for now.
