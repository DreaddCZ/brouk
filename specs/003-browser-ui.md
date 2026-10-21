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
- When tabs do not fit, they shrink down to just their icon. The close button (✕) stays visible on every tab at every width and is visually separated from the rest of the tab, so the user does not close a tab by accident when trying to drag it.
- Tab groups: each profile's tabs in a window form one group, labelled with the profile's name and colour (see 005). Clicking the group label collapses or expands the group, as in Chrome.
- Light and dark theme following the OS by default.
- Clear indication of the profile: every tab shows its profile group, and the toolbar shows the active tab's profile colour.
- Bookmarks, history and downloads views.

## Acceptance criteria

- [ ] Basic browsing: open, close, reorder and switch tabs; navigate; reload; back/forward.
- [ ] All toolbar actions have a keyboard shortcut, and shortcuts can be changed in settings.
- [ ] Command palette opens with a shortcut and can run actions and switch tabs.
- [ ] Tabs are shown in a horizontal strip.
- [ ] When tabs do not fit, they shrink down to their icon.
- [ ] Every tab's close button is visible at every tab width and visually separated from the tab's drag area.
- [ ] A tab group can be collapsed and expanded by clicking its label.
- [ ] Every tab shows which profile it belongs to (its group's name and colour), and the toolbar shows the active tab's profile colour.
- [ ] Theme follows the OS light/dark setting, with manual override.

## Out of scope

- Detailed visual design (separate design work).
- Vertical tabs — planned for later, in a separate spec.
- Workspaces (named, switchable sets of tabs) — not planned for now; profile groups cover the need.

## Configuration

Theme, toolbar items, shortcuts, new tab page content.

## Open questions

- When every tab is already down to icon + close button and they still do not fit: scroll the strip (with a "list all tabs" button), or something else?
- Split view (two pages side by side)? If yes, as its own spec.
- What should the new tab page show? (Suggested: near-empty page with the address bar focused, optionally a few pinned sites; configurable to any URL.)
- Should this spec also cover session restore on startup, pinned tabs, and reopening closed tabs?

## Notes

- 2026-10-21 (decision in 001): hybrid UI shell. Windows, tabs, toolbar, address bar and menus are native (Objective-C++/AppKit on macOS); settings, history, bookmarks, downloads and other internal pages are HTML/TypeScript pages rendered in CEF. This answers "native look or custom cross-platform look" for the main window chrome; internal pages follow the same theme.
- 2026-10-21 (UI and profiles review): horizontal tabs first, vertical later. Tab groups are profile groups (one group per profile per window, see 005), collapsible like in Chrome. Separate workspaces are not needed for now.
