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

## What is recorded

13. Open a page that forwards by itself (e.g. a sign-in link that bounces through a login host, or any page with a meta refresh). Expected: only the page it ends on appears in Recently Visited and in address bar suggestions; the forwarding page does not.
14. Type the address of a page that forwards by itself and press Return; later type its start again. Expected: the page it ended on is completed inline (the visit counts as typed).
15. Settings → History → Never record these sites: enter `login.microsoftonline.com, *.okta.com` (or any site you visited). Expected: pages of those sites already in the history disappear from suggestions and Recently Visited, in every profile; visiting them again does not record them.
16. Enter an invalid value (e.g. `not a site`). Expected: an error under the field; the previous list stays.
17. Remove a site from the list and visit it. Expected: it is recorded again.

