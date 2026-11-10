# 031 – Vertical tabs

- **Status:** draft
- **Priority:** P2
- **Depends on:** 003, 005, 026; 030 if tab groups come first

## Goal

With many tabs and several profiles in one window, a horizontal strip shrinks tabs to icons and collapses groups (003). A sidebar of tabs on the left shows full titles, scales to many tabs, and leaves room for profile groups and the tab groups of 030. 003 plans vertical tabs as a separate spec; this is it.

## Description

Rough draft: options and questions to be decided later.

- A setting switches the tab strip from the top to a **sidebar on the left** of the window (Chrome, Edge and Firefox all have this).
- The sidebar lists profile groups top to bottom, each with its label (name, colour, number of tabs) and its "+" button; the default profile's group is at the bottom (003). Groups collapse as today.
- Each tab is one row: icon, title, and the indicators of the horizontal strip (audio, paused, developer tools); close button always visible (003). Pinned tabs at the top of their profile group.
- Everything that works on horizontal tabs works here: tab menu, dragging (026, vertical instead of horizontal), command palette, shortcuts, session restore.
- With 030: tab groups appear as indented, collapsible sections inside their profile group.

### Options

- **Width:** fixed; resizable by dragging the edge (remembered); or collapsible to an icon-only rail that expands on hover (Edge).
- **Toolbar:** keep the toolbar (address bar, buttons) above the web page, with the sidebar beside it (Edge); or move the toolbar to the top of the sidebar (Arc), giving the page the full height.
- **Window title area (macOS):** traffic-light buttons in the sidebar's top, or a slim title bar above everything.
- **Scope of the setting:** one global setting; per window (View menu, remembered per window by session restore); or per profile (does not fit mixed windows well).

## Acceptance criteria

To be written once the open questions are answered.

## Out of scope

- Tabs on the right or at the bottom (unless asked for).
- A tab tree (tabs nested under the tab that opened them, as in Tree Style Tab).

## Configuration

To be decided; candidates: tab position (top / left, default top), sidebar width, collapsed-to-icons on or off.

## Open questions

1. Which width option, and is collapsing to icons needed?
2. Does the toolbar stay above the page or move into the sidebar?
3. Global setting, per window, or both (global default plus a per-window toggle in the View menu)?
4. Overflow: the sidebar scrolls. Should the active profile group stay visible (sticky label) while scrolling, and do other groups still collapse automatically as in 003?
5. Should the tab overflow rules of 003 (icon-only, auto-collapse of other groups) apply at all in the sidebar, or only scrolling?
6. Shortcut to toggle between top and left (Chrome has none by default; Edge uses Ctrl+Shift+,)?
7. Windows: same layout, or follow Edge's vertical tabs more closely there?
8. Order of work with 030: build tab groups first (they shape the sidebar's sections), vertical tabs first, or both in one spec?

## Notes

- 2026-11-10: created as a rough draft at the user's request, together with 030. The user wants to think about tab groups (030) before deciding the vertical layout, since groups inside a profile group fit a sidebar better than the top strip.
