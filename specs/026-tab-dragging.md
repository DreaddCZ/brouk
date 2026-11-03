# 026 – Dragging tabs and profile groups

- **Status:** in-progress
- **Priority:** P1
- **Depends on:** 003 (browser UI), 005 (profiles), 024 (Windows app)

## Goal

Tabs and profile groups can only be dragged within their own window today, and groups cannot be reordered at all. Working with several profiles side by side needs the same freedom Chrome gives: put the groups in the order that suits the task, pull a page out into its own window to see two pages next to each other, and gather tabs from several windows into one.

## Description

Everything works as in Chrome, on macOS and Windows, with Brouk's profile groups (one group per profile per window; the default profile's group always last) in place of Chrome's tab groups.

### Reordering profile groups

- Dragging a group's label moves the whole group (all its tabs, pinned ones included) before or after another group in the same window; the other groups make room while it is dragged, as tabs do.
- The default profile's group stays last: other groups can be placed anywhere before it, and its own label cannot be dragged within the window.
- A group keeps its collapsed or expanded state while it is moved.

### Moving tabs to another window or a new window

- Dragging a tab up or down out of the tab strip detaches it. Dropped over another Brouk window's tab strip, it joins that window at the drop place; dropped anywhere else, it opens in a new window at that place, with the size of the window it came from.
- The tab keeps its profile: it joins its profile's group in the target window, which is created if that window has none (placed where the tab was dropped, always before the default group). Dropping it inside another profile's group asks to move it to that profile, as within a window (005).
- The page is moved, not reloaded: its content, scroll position, back/forward history, sign-ins, playing media and form input stay as they were. Its per-tab state goes with it: pinned, muted, paused, zoom and dev tools on/off (006). Its opener relation is dropped, as in Chrome.
- Dragging the only tab of a window moves the whole window instead; over another window's tab strip it joins that window and its own window closes.
- A window left with no tabs (only the empty default group) closes, as in Chrome.

### Moving a whole group to another window or a new window

- Dragging a group's label out of the tab strip detaches the whole group; it is dropped into another window or a new window the same way as a tab. In another window its tabs join that window's group of the same profile (one group per profile per window), at the drop place.
- The default profile's group can be dragged out this way too (its tabs move; the empty group stays in the source window).

### Menus

- The tab's right-click menu gets Chrome's **Move Tab to Another Window** submenu: **New Window**, then every other browser window (named by its active tab's title).
- The group label gets a right-click menu with **Move Group to New Window** and the same window list, as Chrome's group menu has "Move group to new window".

## Acceptance criteria

Each criterion holds on macOS and on Windows.

- [ ] Dragging a group's label before or after another group moves all its tabs there, in their order; pinned tabs stay pinned at the start of the group; the collapsed state is kept.
- [ ] No group can be placed after the default profile's group, and the default group's label cannot be dragged within its window.
- [ ] A tab dragged out of the strip and dropped outside any tab strip opens in a new window at the drop place; the page is not reloaded (a playing video keeps playing, typed form text and the back/forward history stay).
- [ ] A tab dropped on another window's tab strip joins that window at the drop place, in its own profile's group (created before the default group if missing), and becomes the active tab there.
- [ ] A tab dropped inside another profile's group of the target window asks to move it to that profile, as within a window.
- [ ] Pinned, muted, paused, zoom and dev tools state of a moved tab are kept.
- [ ] Dragging the only tab of a window moves the window; dropping it on another window's tab strip moves the tab there and closes the empty window.
- [ ] A group's label dragged out of the strip moves all the group's tabs to a new window, or merges them into the target window's group of the same profile at the drop place.
- [ ] The source window closes when no tabs are left in it.
- [ ] The tab menu's "Move Tab to Another Window" and the group menu's "Move Group to New Window" / window list do the same as dragging.
- [ ] Group order and moved tabs are kept when the session is restored (013).
- [ ] Help describes dragging tabs and groups (016).

## Out of scope

- Selecting several tabs and dragging them together (Brouk has no multi-tab selection yet).
- Dropping tabs into internal page windows (settings, profile manager), popup windows or DevTools windows; only browser windows with a tab strip take tabs.
- Dragging tabs between two running copies of Brouk (e.g. a test copy and the user's copy).
- Vertical tabs (003, later).

## Configuration

None.

## Open questions

None.

## Notes

- 2026-11-03 (user's decisions): asked for by the user (beta feedback): reorder profile groups by dragging their label, and drag pages out of the window into another window or a new one, as in Chrome. The default profile's group stays last; whole groups can also be dragged to other windows; macOS and Windows; built next. Spec set to `ready`.
- Chrome behaviour followed: tab drag out of the strip creates a window that follows the pointer; the only tab drags the window; tab groups drag by their header; menus "Move tab to another window" and "Move group to new window". Brouk merges a group into the target window's group of the same profile, because a window has one group per profile (005).
- Verify during implementation: moving a CEF browser to another native window keeps the page alive (macOS: re-parenting the browser's `NSView`; Windows: `SetParent` of the browser's `HWND`, then `NotifyMoveOrResizeStarted` and `WasResized`); tab-modal panels (permission, password, sign-in dialogs) and the dev tools dock must move with the tab or be closed first.
- 2026-11-03 (implementation): verified on macOS that moving the tab's view (with the CEF browser's `NSView` and the dev tools dock) into another window keeps the page live: a running counter and typed form text survive, and nothing reloads. On Windows the tab's container window is re-parented with `SetParent`, the docked dev tools go along (a dock in its own window stays there) and CEF is told with `NotifyMoveOrResizeStarted`. Open tab-modal panels live inside the tab and move with it; the window's password and save prompts close when their tab leaves.
- Chrome behaviour followed in details: the drag image is a picture of the tab or label; Esc cancels a drag; a tab dragged back over its own strip shows the mark and goes there; the menu's window list names each window by its active tab with "and N more"; "New Window" is disabled for a window's only tab; a maximised window dragged by its only tab is restored under the mouse (Windows).
- Internal pages (history, bookmarks, downloads, passwords) act on the window holding their tab at the time, found by the tab's id, so they keep working after the tab moves (found while implementing; before, they kept the window they were opened in).
- macOS: Tab → Move Tab to New Window has a configurable shortcut `shortcuts.move_tab_new_window` (unset by default, as in Chrome). Windows has no menu bar, so the action is in the tab menu only.
- Testing: the core's group moves and moved-tab inserts have unit tests; dragging gestures are tested by hand (`docs/testing/026-tab-dragging.md`), as posted mouse events do not reach a separate test copy of Brouk on macOS.
