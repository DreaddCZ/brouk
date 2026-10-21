# 010 – Window picker for opening links

- **Status:** idea
- **Priority:** P2
- **Depends on:** 001, 003, 005

## Goal

When a link is opened (from another app or inside the browser), let the user choose in which existing window it opens, instead of the browser picking one.

## Description

The browser keeps a live model of all its windows and tabs, including which display each window is on and whether it is on the current desktop/Space. When a link arrives, the browser first asks the user which window should receive it.

## Acceptance criteria

To be written.

## Out of scope

To be defined.

## Configuration

To be defined (e.g. always ask / default window / rules per domain or source app).

## Open questions

- Which link sources trigger the picker: links from other apps only, or also links inside the browser (new tab / new window)?
- What does the picker show: window titles, tab lists, page thumbnails, profile colour, display and Space?
- Can the user also choose "new window" or a specific profile from the picker? (Relates to the open question in 005.)
- After choosing a window on another Space: switch to that Space, or move the window to the current one?

## Notes

- Displays: fully supported by public APIs on macOS and Windows.
- macOS Spaces: public APIs only tell whether a window is on the *active* Space and notify when the active Space changes. Listing all Spaces, knowing which Space a window is on, or moving a window to another Space needs private APIs (fragile, may break with macOS updates, not allowed in the Mac App Store).
- Windows virtual desktops: a public API reports a window's desktop and can move our own windows between desktops; listing desktops and their names is undocumented.
- Page thumbnails do not need screen recording permission: the browser renders its own content.
