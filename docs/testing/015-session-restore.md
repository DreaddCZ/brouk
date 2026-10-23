# 015 – Session restore: manual test checklist

1. Settings → Startup → On startup shows "Continue where you left off" (the default).
2. Open two windows: one with tabs of two profiles (one pinned, one group collapsed), another with a few tabs; move and resize them. Quit (⌘Q) and start Brouk. Expected: both windows come back with their size and position, tabs in order in their profile groups, the pinned tab pinned, the collapsed group collapsed, and the tab that was active active.
3. Look at a restored window's other tabs. Expected: they show their titles but do not load until you click them (no spinner before that); clicking loads the page.
4. Close a restored tab you never opened. Expected: it closes at once.
5. Close one window, quit, start. Expected: only the remaining window comes back.
6. Close all windows (Brouk keeps running), quit, start. Expected: a new window with a new tab.
7. Start Brouk from Terminal with a URL (`…/brouk https://example.com`). Expected: the last session comes back and the URL opens as a new tab in the front window.
8. Pop-up windows (e.g. an OAuth sign-in window) are not restored.
9. Delete a profile, then quit and start with a session that had its tabs. Expected: they are gone (closed with the profile); if a session file still names it, those tabs open in the default profile.
10. Set "Open a new tab", quit, start. Expected: one window with a new tab.
11. Simulate a crash: with "Open a new tab", in Terminal run `killall -9 brouk` while Brouk runs, then start it. Expected: a "Restore pages? Brouk didn't shut down correctly." bar with Restore and ×. Restore brings back the windows and closes the empty new window; × just hides the bar.
12. Crash again with "Continue where you left off". Expected: the session comes back without asking.
13. Unplug an external display a window was on, then start. Expected: the window opens on the remaining screen (its old position is ignored).
