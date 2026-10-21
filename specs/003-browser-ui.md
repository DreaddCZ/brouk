# 003 – Browser UI

- **Status:** draft
- **Priority:** P1
- **Depends on:** 001, 004

## Goal

A clean, minimal browser interface built for focused work, which the user can rearrange and control from the keyboard.

## Description

Core UI elements:

- Windows with tabs, an address/search bar, back/forward/reload.
- Minimal chrome by default: no promotional content, news feed or unnecessary buttons.
- Keyboard-first: every action reachable by a shortcut; shortcuts remappable.
- A command palette (search for any action, tab, bookmark or setting).
- Layout options, e.g. horizontal or vertical tabs, compact mode, hidden toolbar.
- Light and dark theme following the OS by default.
- Clear indication of the current profile (see 005).
- Bookmarks, history and downloads views.

## Acceptance criteria

- [ ] Basic browsing: open, close, reorder and switch tabs; navigate; reload; back/forward.
- [ ] All toolbar actions have a keyboard shortcut, and shortcuts can be changed in settings.
- [ ] Command palette opens with a shortcut and can run actions and switch tabs.
- [ ] Tab layout (horizontal/vertical) can be switched in settings.
- [ ] The active profile's name/colour is always visible.
- [ ] Theme follows the OS light/dark setting, with manual override.

## Out of scope

- Detailed visual design (separate design work).

## Configuration

Tab layout, theme, toolbar items, shortcuts, new tab page content.

## Open questions

- Vertical tabs, horizontal tabs, or both?
- Workspaces/tab groups for different work contexts?
- Split view (two pages side by side)?
- What should the new tab page show?
- Native look (macOS-style) or a custom cross-platform look?

## Notes
