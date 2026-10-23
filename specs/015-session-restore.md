# 015 – Session restore

- **Status:** done
- **Priority:** P1
- **Depends on:** 003, 005

## Goal

Start where work stopped: after quitting (or a crash), Brouk brings back the windows and tabs that were open, in their profiles, so the user does not have to reopen them.

## Description

### On startup

A setting **"On startup"** (Settings → Startup), as in Chrome:

- **Continue where you left off** (default) — the windows open when Brouk last quit come back.
- **Open a new tab** — one new window with a new tab, as before.

### What comes back

- Every normal browser window open at quit (not pop-up windows), with its size and position.
- In each window: its tabs in order, each in its profile group (005), pinned tabs pinned, collapsed groups collapsed, and the tab that was active.
- Each tab's current page with its title. Not its back/forward list: CEF cannot restore it.
- **Tabs load when first shown.** Only the active tab of each window loads at startup; the others show their title and load when the user switches to them. This keeps startup fast and memory low.
- Tabs of a profile that no longer exists open in the default profile.
- URLs given on the command line (or from other apps) open as new tabs in the front restored window, as in Chrome.
- If no window was open at quit, a new window opens.

### After a crash

The session is saved while Brouk runs, not only at quit, so a crash or power loss loses at most the last few seconds.

- With "Continue where you left off", the session comes back as after a normal quit.
- With "Open a new tab", the new window shows a **"Restore pages?"** bar, as in Chrome: "Brouk didn't shut down correctly." with a **Restore** button (brings the last session back) and a close button.

### Storage

The session is kept in `session.yaml` in the app's data folder (machine-written, like the settings; not meant for editing). It holds URLs and titles of open tabs, so it stays only on this Mac like the history.

## Acceptance criteria

- [x] With "Continue where you left off" (default), quitting with two windows of tabs in two profiles and starting again brings back both windows with their tabs in order, in their profiles, with pinned tabs, collapsed groups, the active tab, and window size and position.
- [x] Only the active tab of each restored window loads at startup; another tab loads when it is first shown, and shows its title before that.
- [x] With "Open a new tab", starting opens one window with a new tab.
- [x] After a crash, "Continue where you left off" restores the last session; with "Open a new tab", a "Restore pages?" bar offers to restore it.
- [x] URLs passed on the command line open as tabs in the front restored window.
- [x] Pop-up windows are not restored; tabs of a deleted profile open in the default profile.

## Out of scope

- Back/forward history of restored tabs (CEF limitation).
- Form contents, scroll positions, developer tools state.
- Several saved sessions or named workspaces (003: not planned).
- Syncing sessions between Macs.

## Configuration

| Setting | Default | Scope |
|---|---|---|
| On startup (`startup.restore`): continue where you left off / open a new tab | continue where you left off | global |

## Open questions

None.

## Notes

- 2026-10-23 (user's decisions): asked for restoring the pages open at the last quit; default "Continue where you left off" (Chrome's default is a new tab); background tabs load when first shown; after a crash with "Open a new tab", offer "Restore pages?" as Chrome does. Created as its own spec because 003 left session restore open ("to be resolved during testing") and is done.
- 2026-10-23 (implementation): `src/core/session` (data, YAML, atomic save, startup decision) with tests; `session.yaml` is written 1.5 s after a change (tabs, pages, order, pinning, collapsing, window frame) and once more at quit with `clean_exit: true`; the start writes `clean_exit: false` first, so the next start can tell a crash. Windows are saved back to front, so the front window is restored last and ends up in front. A saved frame that is no longer on any screen is ignored. Restored background tabs have no browser until first shown (they show the saved title, or the address); closing one that was never shown just removes it. The profile manager at startup (005) is not shown when a session is restored. After a crash with "Open a new tab", Restore brings the windows back and closes the start's window if it still holds only an unused new tab. Known for testing: the "Restore pages?" bar lies over the top of the page until it is closed.
- 2026-10-23 (user's testing): acceptance criteria verified by the user; spec set to `done`.
