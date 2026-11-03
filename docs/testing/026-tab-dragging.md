# 026 – Dragging tabs and profile groups: manual test checklist

Run each step on macOS and on Windows. Set up two profiles besides the default one (e.g. Work and Home), and a window with two Work tabs (one pinned), one Home tab and two default-profile tabs. On one Work tab, play a video (or open a page with a running counter) and type text into a form field without submitting it.

1. Drag the Home group's label left of the Work group. Expected: the other groups make room while dragging; on release all Home tabs sit before Work, in their order. A click on a label (without dragging) still collapses or expands it.
2. Collapse Work, then drag its label to another place. Expected: it stays collapsed. Drag Work after the default group. Expected: it stops before the default group, which stays last.
3. Drag the default group's label left or right. Expected: it does not move.
4. Drag the playing Work tab down out of the strip and drop it on the desktop. Expected: a picture of the tab follows the mouse; a new window opens under the mouse with the old window's size, holding only that tab in a Work group; the video keeps playing, the typed text is still there, Back still works. The old window keeps its other tabs.
5. Drag that tab back onto the first window's tab strip, between the other Work tabs. Expected: a mark shows where it goes; it lands there and is active; the now empty window closes.
6. Drag a Home tab onto the Work group of another window. Expected: Brouk asks to move it to Work (the page would reload); No leaves it in a new Home group of that window.
7. Drag a Work tab to a window with no Work group, dropping it between two other groups. Expected: a Work group is created there, never after the default group.
8. Pin a tab, mute another, pause a third (task manager or tab menu), zoom one to 125 % and switch dev tools on in one; move each to another window. Expected: each keeps its state; the dev tools dock works in the new window.
9. In a window with one tab, drag that tab. Expected: the whole window follows the mouse. Drop it on the desktop: the window stays there. Drag it again onto another window's strip: the tab joins that window and the empty window closes.
10. Drag a group's label down out of the strip and drop it on the desktop. Expected: all its tabs open in a new window, in order, pinned ones pinned. Drag the label of a group onto another window that has the same profile's group. Expected: the tabs join that group at the mark.
11. Drag the default group's label (with tabs) out of the strip. Expected: its tabs move; the source window keeps its empty default group if other groups are left.
12. Start a drag out of the strip and press Esc before releasing. Expected: nothing moves (a window being dragged goes back to where it was).
13. Right-click a tab → Move Tab to Another Window → New Window, then → the other window's name. Expected: the same as dragging (the tab moves without reloading). The item is disabled for a window's only tab.
14. Right-click a group label → Move Group to New Window and → Move Group to Another Window → a window. Expected: the same as dragging the label.
15. macOS: Tab → Move Tab to New Window (and its shortcut, if set in Settings → Shortcuts). Expected: the active tab moves to a new window.
16. Open the history page in a tab, move it to another window, and open a page from it. Expected: the page opens in the window the history tab is in now.
17. Quit and start Brouk. Expected: the reordered groups and moved tabs come back as they were (015).
18. Help → Tabs and windows → Moving tabs and groups describes all of the above.
