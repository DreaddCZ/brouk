# 013 – History: manual test checklist

Address bar suggestions and recording are in `003-browser-ui.md` (steps 37–48).

## History menu and closed tabs

1. Open three tabs with pages, close the middle one (⌘W), press ⇧⌘T. Expected: it comes back at the same place, with the same page, and is active.
2. Pin a tab, close it, ⇧⌘T. Expected: it comes back pinned at the start of its group.
3. Close tabs in two profiles, then open History → Recently Closed. Expected: newest first, each with its profile's colour dot; choosing one reopens it in its profile.
4. Close a window with several tabs (red button or ⇧⌘W), then ⇧⌘T. Expected: a new window with those tabs in their profiles.
5. Close a tab, then close its window, then reopen the tab from Recently Closed. Expected: it opens in the front window.
6. Close more than 10 tabs. Expected: Recently Closed lists the last 10.
7. Delete a profile that has closed tabs in the list. Expected: they disappear from Recently Closed.
8. With nothing closed, History → Reopen Closed Tab is greyed out and Recently Closed says "No Closed Tabs".
9. New tabs that were never used (blank) are not listed after closing.
10. History → Recently Visited shows the last pages of the active tab's profile (switch to a tab of another profile: its pages). Choosing one opens it in a new tab of that profile.
11. History → Home (⇧⌘H). Expected: the current tab shows the new tab page and the address bar is focused.
12. Remap Reopen Closed Tab and Home in Settings → Shortcuts. Expected: the new shortcuts work.
