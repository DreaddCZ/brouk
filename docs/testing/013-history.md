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

## History page

18. Press ⌘Y (or History → Show Full History). Expected: a "History" tab with a clock icon opens next to the current tab, in its profile group; on an unused new tab, that tab becomes the history page. ⌘Y on the history tab does nothing.
19. The page lists one row per exact host (e.g. two different `*.atlassian.net` sites are two rows), with visits, pages and last visit, newest first; each row has its profile's colour dot. Sort by Visits and by Site.
20. Choose a profile in the Profile menu. Expected: only its sites; "All profiles" shows all (the same host in two profiles is two rows).
21. Click a site. Expected: its pages appear below it (title, path, visits, last visit); click again to fold them.
22. "By day": visits under Today, Yesterday and dates; "Show more" loads older ones. Reopen the page later: it remembers the view.
23. Click a page title in the same profile as the history tab. Expected: the history tab becomes that page. In another profile's entry: a new tab in that profile's group. ⌘-click: a background tab.
24. Check two rows' checkboxes, ⇧-click a third to select the range, press Delete and confirm. Expected: they disappear (site rows remove all their pages). ✕ on a single row deletes it at once.
    Expand a site and check its checkbox. Expected: its pages show checked and greyed out (they are deleted with the site) and the count does not include them; unchecking the site frees them again.
25. In the history tab, type a web address in the address bar. Expected: the tab is replaced by a normal tab with that page (no Back to the history page). Type `brouk://history` in a normal tab: it becomes the history page. `brouk://settings` opens the Settings window.
26. Close the history tab and press ⇧⌘T; quit and start again with "Continue where you left off". Expected: the history tab comes back as the history page.

## Search

27. Type two words from a page title. Expected: only pages containing both (in title or address); accents and case do not matter. Clear the box: everything again.
28. `"exact phrase"`, `-word`, `jira OR confluence`: phrase only, word excluded, either word.
29. `site:atlassian.net` shows all its subdomains; `host:companya.atlassian.net` only that one; `-site:atlassian.net` everything else.
30. `title:sprint`, `url:/browse/`: only in the title / only in the address.
31. `after:7d`, `before:2026-10-01`, `on:yesterday`: in "By day" only visits in that time; in "By site" the counts show only matching visits.
32. `visits:>3`, `profile:Work` (a profile name), `type:typed`: pages visited more than 3 times, that profile, visits typed in the address bar.
33. Combine several, e.g. `site:github.com after:30d -type:reload`.
34. Type `after:yesterdayy`. Expected: it is underlined with a short reason under the box, and the rest of the search still applies.
35. Use each chip (Site, Date, Profile, Visits, Type). Expected: it adds the same filter text to the box and the list updates.
36. With a search active, expand a site: only its matching pages are listed. Search for something that does not exist: "Nothing matches the search."

## SQL

37. Press "SQL" next to the search box. Expected: the editor appears with the views and their columns and example queries on the right; the search and lists are hidden. Press "SQL" again to go back.
38. Click an example. Expected: it runs and shows a table; the status shows the number of rows and the time.
39. `SELECT profile, host, count(*) FROM visits GROUP BY profile, host ORDER BY 3 DESC` with ⌘↩. Expected: sites of all profiles.
40. Click an address in the results. Expected: it opens in the profile of the row's `profile` column (or the history tab's profile if there is none).
41. "Copy as CSV", then paste into a text editor or Numbers. Expected: the header and rows, commas and quotes handled.
42. `DELETE FROM pages`, `UPDATE pages SET title = 'x'`, `DROP TABLE pages`, `PRAGMA table_list`, `ATTACH 'x' AS y`, `SELECT 1; SELECT 2`. Expected: each is refused with a message; the history is unchanged.
43. `SELECT * FROM sqlite_master`. Expected: refused (only the two views can be read).
44. `WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i+1 FROM n) SELECT count(*) FROM n`. Expected: stopped after 2 seconds with a message. Without `count(*)` (`SELECT i FROM n`): 1,000 rows and "(stopped at 1,000)".
45. A mistake (`SELECT nope FROM pages`). Expected: SQLite's message ("no such column: nope").

## Clean up

46. On the history page press "Clean up…". Expected: a dialog with Profile (this tab's profile selected), Time (All time), "Keep selected sites, delete everything else" selected, and the sites listed most visited first; the 10 most visited are checked and marked "suggested".
47. Uncheck one suggested site and check a small one. Expected: the summary updates ("Deletes N visits from M sites.").
48. Press Clean up. Expected: it asks "Delete N visits from M sites? This cannot be undone."; Delete removes them, the dialog closes and the page shows only the kept sites.
49. Open Clean up again. Expected: the sites kept last time are checked (the keep list), plus the suggestions.
50. "Delete selected sites": nothing is checked; check two sites and clean up. Expected: only those two are deleted; the keep list is unchanged.
51. Time "Last 24 hours" with a site visited today and last week, deleting it. Expected: only today's visits go; the site stays with its older visits.
52. Profile "All profiles". Expected: rows of every profile with their colour dots; each profile's own keep list and top 10 are preselected; deleting works per profile.
53. Type in the filter box. Expected: the list narrows to matching sites; checked sites that are hidden by the filter keep their state.
54. Escape or Cancel closes the dialog without deleting.

## Clear browsing data

55. History → Clear Browsing Data… (⇧⌘⌫). Expected: the history tab opens with the dialog; Profile is this tab's profile, Time "Last 24 hours", Browsing history and Cached images checked, Cookies unchecked. Counts appear: visits and sites, number of cookies, cache size.
56. Settings → History → "Clear browsing data…" opens the same dialog; so does the button on the history page.
57. Clear history for "Last hour". Expected: it asks again, then only the last hour's visits are gone; older history stays.
58. Sign in to a site in a test profile, then clear "Cookies and site data" for "All time" in that profile. Expected: you are signed out there; other profiles stay signed in. A site's local storage is empty afterwards (check with developer tools → Application).
59. Clear cookies for "Last hour" after signing in to a new site an hour after an older sign-in. Expected: only the new sign-in is gone.
60. Clear only "Cached images and files". Expected: the cache size drops to near zero (the whole cache, whatever the time range).
61. Profile "All profiles": each profile's numbers are listed; clearing affects every profile.
62. Use a profile with no open tab. Expected: the counts and clearing still work (a hidden helper is used). If the cookie count says "number unknown", report it.

