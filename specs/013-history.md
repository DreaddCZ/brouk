# 013 – History

- **Status:** in-progress
- **Priority:** P1
- **Depends on:** 001, 003, 005, 010

## Goal

Record visited pages per profile, so the user can find them again, the address bar can suggest them, and closed tabs can be reopened. It works like Chrome's history, plus one view across all profiles.

## Description

### What is recorded

- History is stored **per profile** (005). Each profile's history is separate data. It is deleted with the profile. Including it in a profile export is spec 022.
- **Recorded:**
  - Every top-level navigation to an `http`, `https` or `file` URL, with URL, page title, visit time and how the visit started (typed, link, bookmark, reload…).
  - Through a redirect chain, only the final URL is shown. This covers redirects by the server and pages that forward to another page by themselves right after loading (meta refresh, script); the page that forwarded is not kept.
  - Each page's favicon, cached per profile. Bookmarks (011) use these icons too.
- **Not recorded:**
  - `brouk://` pages, `data:` and `blob:` URLs, error pages, and downloads.
  - Navigations inside frames other than the main frame.
  - Pages of sites on the user's **"never record" list** (a setting, e.g. sign-in pages such as `login.microsoftonline.com` or `*.okta.com`). The list uses the URL patterns of 006 (`example.com`, `*.example.com`, `example.com/path/`). Adding a site also removes its pages already in the history.
- **Retention:** entries older than **90 days** are deleted automatically, as in Chrome. The limit is a setting.

### History page

- An internal HTML page, `brouk://history`, opened with **⌘Y** or History → Show Full History.
- It shows the **history of all profiles**:
  - a **profile filter** (All / one profile), set to All when the page opens;
  - every site and entry shows its profile's colour.

**Grouped by site (default view), to reduce clutter:**
- One row per **exact host**. For example, `companya.atlassian.net` and `companyb.atlassian.net` are two separate rows, and `github.com` is one row.
- Each row shows the favicon, the host, the number of visits, the number of different pages, and the last visit.
- Rows are sorted by last visit, newest first. They can also be sorted by number of visits or by host name.
- With the profile filter set to All, the same host visited in two profiles gives two rows, each with its profile's colour.
- **Expanding a row** shows that host's pages, newest first: title, path and last visit. A page visited several times is listed once, with its visit count.
- A **"By day"** toggle switches to Chrome's list of single visits grouped by day.

**Opening an entry:** it opens in the profile it was visited in. When that is not the current tab's profile, the window is chosen as in 010. ⌘-click opens it in a new tab.

**Actions:**
- delete a page, a whole site row, or several selected rows or pages (⌘-click, ⇧-click);
- "Clean up…" (see below);
- "Clear browsing data…".

### Clean up

The "Clean up…" dialog removes clutter while keeping the sites the user cares about. It is opened from the history page.

- **Choose the scope:**
  - **profile** — the active tab's profile (preselected), another profile, or all profiles;
  - **time range** — last 24 hours, last 7 days, last 4 weeks, all time.
- **The dialog lists every host** in that scope, with a checkbox, visit count and last visit. Hosts are sorted by visit count, and the list can be filtered by typing.
- **Two modes:**
  - **Delete selected sites** — removes the checked hosts.
  - **Keep selected, delete everything else** (the default) — removes all hosts except the checked ones.
- **Preselected in "keep" mode:**
  - the profile's **keep list** — the hosts kept the last time, remembered per profile;
  - plus the **10 most visited hosts** of the scope, marked "suggested".
  - The user can check and uncheck freely. On confirm, the checked hosts become the profile's new keep list.
  - With "all profiles", each profile uses its own keep list and suggestions.
- **Confirm** shows how many entries and sites will be deleted. Deleting cannot be undone.
- Clean up runs only when the user starts it; there is no automatic clean up. The 90-day retention still applies.

### Search

The search box filters the current view: in "by site" view it shows the hosts and pages that match; in "by day" view, the matching visits.

**Plain words** match the title or the URL. All words must match (AND), and the match ignores case and accents.

**Field filters,** which can be combined:

| Filter | Meaning |
|---|---|
| `"exact phrase"` | phrase in title or URL |
| `-word`, `-site:x` | exclude |
| `a OR b` | either term |
| `site:atlassian.net` | host is this or a subdomain of it |
| `host:companya.atlassian.net` | exactly this host |
| `title:sprint` | in the title only |
| `url:/browse/` | in the URL only |
| `after:2026-10-01`, `before:…`, `on:…` | visit date; also relative values like `after:7d` |
| `visits:>3` | pages with more than 3 visits (also `<`, `=`) |
| `profile:Work` | profile by name |
| `type:typed` | how the visit started: `typed`, `link`, `bookmark`, `reload` |

**Filter chips** under the search box (Site, Date, Profile, Visits, Type) add filters without typing; they write the same text into the box. An invalid filter is underlined with a short hint, and the rest of the query still applies.

**SQL (advanced):** an "SQL" toggle replaces the search box with a query editor for read-only SQL (SQLite dialect).

- **Views available:**
  - `pages(profile, host, url, title, visit_count, typed_count, last_visit)`
  - `visits(profile, host, url, title, visit_time, transition)`
  - They cover every profile; `profile` is the profile's name.
  - The views and their columns are listed next to the editor, with a few example queries.
- **Results** are shown as a table. URLs are clickable and open as described above. Results can be copied as CSV.
- **Safety:**
  - queries run on a separate **read-only** connection that can see only these views;
  - anything other than `SELECT` is rejected;
  - a query stops after **2 seconds** and returns at most **1,000 rows**;
  - errors are shown as SQLite reports them.

### History menu

The menu bar has a **History** menu. It contains:

- Back (⌘[) and Forward (⌘])
- Home (⇧⌘H) — opens the new tab page
- **Reopen Closed Tab** (⇧⌘T) — reopens the most recently closed tab or window, in its profile and position. If the window it came from is gone, it goes into the current window.
- **Recently Closed** — the last 10 closed tabs and windows of this run, each with its profile's colour.
- **Recently Visited** — the last 10 pages of the active tab's profile.
- Show Full History (⌘Y)

The list of closed tabs is kept until the app quits. Session restore at startup is not part of this spec (see 003 notes).

### Address bar suggestions

History is the first source of address bar suggestions (003).

- Suggestions come only from the active tab's profile.
- They are ranked like Chrome: typed visits, visit count and recency.
- A URL typed earlier is completed inline.
- ⇧⌫ on a highlighted history suggestion removes that page from the profile's history.

### Clear browsing data

The dialog is opened from the history page, from History → Clear Browsing Data… (⇧⌘⌫), and from settings. It has:

- **Profile:**
  - the active tab's profile (preselected), another profile, or all profiles;
  - for each profile, the dialog shows how much data will be cleared.
- **Time range:** last hour, last 24 hours, last 7 days, last 4 weeks, all time.
- **Checkboxes:**
  - Browsing history (on);
  - Download history (on) — the list only, not the files (014);
  - Cookies and site data (off);
  - Cached images and files (on).

Clearing cookies logs the profile out of sites. The checkbox says so.

### Profile export

Moved to [022](022-history-export.md).

### Storage

- One **SQLite** database per profile: `profiles/<id>/history.db`. It holds visits, favicons and the downloads list (014), as Chrome's History file does.
- SQLite is public domain. It is added as a third-party library under the rules in `CLAUDE.md`, using the official amalgamation, and listed in `third_party/README.md`.
- The database is written in WAL mode, so a crash loses at most the last few visits.

## Acceptance criteria

**Recording**
- [x] Visiting a page in profile A records it in A's history only; it is not suggested and not listed under profile B.
- [x] Redirects show only the final URL, also when a page forwards to another one by itself; brouk://, data:, blob: and error pages are not recorded.
- [x] Pages matching the "never record" list are not recorded, in any profile; adding a site removes its existing pages from the history.
- [ ] Entries older than the retention limit (90 days by default) are removed automatically.

**History page**
- [x] ⌘Y opens the history page grouped by exact host (companya.atlassian.net and companyb.atlassian.net are separate rows), with visit counts, last visit and profile colour; a row expands to its pages; it can be filtered by profile and sorted by last visit, visits or host.
- [x] The "By day" toggle shows single visits grouped by day.
- [x] Clean up in "keep selected" mode deletes every host in the chosen profile and time range except the checked ones, and shows the counts before confirming.
- [x] Clean up preselects the profile's remembered keep list plus the 10 most visited hosts; the hosts kept become the new keep list.
- [x] Clean up in "delete selected" mode deletes only the checked hosts.
- [x] Search with plain words matches title or URL, all words required; the filters `site:`, `host:`, `title:`, `url:`, `after:`/`before:`/`on:` (absolute and relative), `visits:`, `profile:`, `type:`, phrases, `-` and `OR` work as described, alone and combined.
- [x] The filter chips add the same filters as typing them.
- [x] The SQL editor runs SELECT queries over `pages` and `visits` for all profiles and shows the results as a table, which can be copied as CSV.
- [ ] Any statement that would change data is rejected, and queries are stopped after 2 seconds or 1,000 rows.
- [x] An entry opened from the history page opens in the profile it was visited in, in the window chosen by the rules of 010.
- [x] A page, a site row, or a multi-selection of them can be deleted.

**Menu and closed tabs**
- [x] ⇧⌘T reopens the last closed tab (or window) in its profile, at its previous position when possible, up to the last 10 tabs or windows of the current run.
- [x] The History menu lists Recently Closed (with profile colours) and Recently Visited (active tab's profile).

**Address bar**
- [x] Address bar suggestions from history come only from the active tab's profile; ⇧⌫ removes a history suggestion from that profile's history.

**Clearing and profiles**
- [x] Clear Browsing Data clears the chosen data types, for the chosen profile or all profiles, within the chosen time range, and leaves other profiles untouched.
- [x] Deleting a profile deletes its history.

## Out of scope

- History sync between devices.
- Chrome's "Journeys" / grouping of history by topic.
- Full-text search of page contents (search covers titles and URLs only).
- Automatic clean up of rarely visited sites (only the retention limit runs automatically).
- SQL that changes data, or SQL over other data (bookmarks, downloads, passwords).
- Session restore at startup and keeping closed tabs after quitting (003, left for testing).
- Import of history from other browsers (in line with 005).
- History in profile export: moved to 022.
- A private/incognito mode.

## Configuration

| Setting | Default | Scope |
|---|---|---|
| Keep history for | 90 days (alternatives: 30 days, 1 year, forever) | global |
| Never record these sites | empty; URL patterns as in 006 | global |
| History in address bar suggestions | on (already in 003) | global |
| History page view | by site (alternative: by day; the page remembers the last choice) | global |
| Clean up keep list | empty; updated by Clean up | per profile |

## Open questions

None.

## Notes

- 2026-10-21: placeholder created from the readiness review. Until this spec is ready, Claude Code may store visited URLs and titles per profile only as far as address bar suggestions (003) need it, in a storage module this spec will later extend.
- 2026-10-22 (history review with the user): agreed on Chrome-like history, 90-day retention, and the history page showing all profiles with a profile filter, like the bookmark manager. Clear browsing data, the History menu and ⇧⌘T reopening closed tabs (current run only) were taken from Chrome. Storage is SQLite per profile. Spec set to `ready`.
- 2026-10-22 (history review, part 2, with the user): to reduce clutter, the history page groups entries by exact host (default view; Chrome's by-day list is a toggle). A manual "Clean up" dialog deletes selected hosts or everything except kept hosts; the keep list is remembered per profile and topped up with the 10 most visited hosts. No automatic clean up. Search uses field filters with chips, plus a read-only SQL editor over `pages` and `visits` views.
- 2026-10-23 (implementation, with 003): storage, recording, retention and the address bar part are done; the history page, menu, closed tabs, clearing and export follow. SQLite 3.54.1 amalgamation (`cmake/BroukSqlite.cmake`), one `profiles/<id>/history.db` per profile in WAL mode, tables `urls` and `visits` (schema version 1; favicons and downloads come as later versions). All database work runs on one dedicated thread (`src/cef/history_service`). A visit is recorded when the main frame finishes loading without an error, at the final URL of server redirects; same-document navigations (history.pushState) count too, as in Chrome. Not recorded: anything other than http, https and file, error pages, downloads, subframes. Client-side redirects (meta refresh, JavaScript) still record both pages for now; to be revisited with the history page. Transitions: typed = entered in the address bar or a history suggestion chosen there (searches are not "typed", as in Chrome), then reload, bookmark, link (links and forms), other (back/forward and the rest). Ranking: how the text matches (start of the address first), then typed visits, visit count and recency; inline completion only for pages typed before. Retention (`history.keep`: 30, 90 (default), 365 days or forever) runs when a profile's history opens and then hourly.
- 2026-10-23 (implementation, History menu): History → Back, Forward, Home (⇧⌘H: the new tab page in the current tab, as in Chrome), Reopen Closed Tab (⇧⌘T), Recently Closed and Recently Visited (submenus). Show Full History (⌘Y) comes with the history page. Closed tabs: a tab closed on its own is one entry; a window closed as a whole (its close button, ⇧⌘W) is one entry with its tabs; closing the last tab of a window keeps that tab. New tab pages are not kept; tabs of a deleted profile are removed from the list; quitting does not add entries. A reopened tab goes back into its window (if still open) at its old place in its profile group, pinned if it was; otherwise into the front window. A reopened window opens as a new window. Only the page comes back, not its back/forward list (CEF cannot restore it). Recently Visited lists the 10 latest pages of the front window's active profile; it is read from the history in the background, so the first opening after start may show "Loading…". Its items open in a new tab of that profile. Both shortcuts are remappable (`shortcuts.home`, `shortcuts.reopen_closed_tab`).
- 2026-10-23 (user's decision, testing feedback): pages that forward to another page by themselves (client redirects) are removed from the history, as Chrome hides them; the user asked for a "never record" site list in addition. Description and acceptance criteria updated with the user's approval.
- 2026-10-23 (implementation, history limits): a forwarding page is recognised by CEF's client-redirect qualifier on the next page's navigation entry (meta refresh, script navigation without a user gesture while or right after loading); its latest visit is removed and the next page keeps how the chain started (typed, link…). Server redirects were already recorded only at their end. "Never record these sites" (`history.never_record`, Settings → History) takes the URL patterns of 006 separated by commas; matching pages are not recorded or retitled, and pages already in a profile's history are removed when the list changes and when that profile's history is opened.
- 2026-10-23 (user's decisions, history page): rows are selected with checkboxes (⇧-click on a checkbox selects a range), as on Chrome's history page; a click on a title opens the page, ⌘-click (or a middle click) opens it in a new tab — this settles the overlap of "⌘-click" for opening and for selecting in the description. The history page opens **as a tab** (⌘Y), in the current tab's profile group; bookmarks (011) and downloads (014) will open the same way.
- 2026-10-23 (implementation, history page): internal pages in tabs run in the internal pages' own browsing context with their bridge, never in a profile's context; typing a web address into such a tab (or opening an entry in it) replaces it with a normal tab of the profile, and typing `brouk://history` replaces a web tab with the page — so web pages never get the bridge and internal pages never run in a profile. `brouk://settings` and `brouk://profiles` typed into the address bar open their windows. ⌘Y reuses an unused new tab and does nothing on the history tab itself. Opening an entry: a plain click opens it in the history tab when that tab is in the entry's profile group, else as a new active tab of that profile in the current window (the window choice of 010 follows with 010); ⌘-click opens a background tab. By site: one row per exact host and profile, with visits, pages and last visit, sortable by last visit, visits or site; rows expand to their pages (title, path, visits, last visit). By day: single visits under "Today", "Yesterday" and dates, 200 at a time ("Show more"). Deleting: ✕ on a row deletes it at once; selected rows are deleted after a confirmation in the page. A site row deletes all its pages, a page row the page, a day row that one visit. Favicons are not stored yet: rows show the site's first letter. The view (by site / by day) is remembered as the setting `history.view`. Search, Clean up and Clear browsing data follow.
- 2026-10-23 (user's feedback): a selected site's expanded pages show checked and disabled, since deleting the site deletes them; they are not counted or deleted on their own.
- 2026-10-23 (implementation, search): the query is parsed and matched in the core (`src/core/history_query`) and run over the whole history on the history thread, not only over the rows shown. A visit is tested as a whole (page, time, type, profile), so date and type filters work in the by-site view too: a site row shows only its matching visits and pages, and an expanded site only its matching pages. Grammar: OR binds closer than the implied AND ("a b OR c" = a and (b or c)); `-` excludes any term or filter; `site:` matches the host and its subdomains (`*.` allowed), `host:` the exact host; dates are local days (`2026-10-01`, `today`, `yesterday`) or times ago (`7d`, `2w`, `3m` months, `1y`); `before:` a day means before that day starts; `visits:` takes `>`, `<`, `=`, `>=`, `<=` or a plain number and counts the page's visits; `profile:` compares profile names (combined with the profile menu); `type:` also accepts `other`. What is not understood is underlined under the box with the reason, and the rest applies. Chips: Site (a host), Date (today, yesterday, last 7 or 30 days, older than 30 days, or a chosen day), Profile, Visits (more than 1/3/10, once) and Type; each appends the same text to the box.
- 2026-10-23 (implementation, SQL): `src/core/history_sql`. Each query gets a fresh in-memory connection; every profile's `history.db` is attached read-only (`mode=ro`), copied into two temporary tables `pages` and `visits`, and detached again, so the query cannot reach the files at all. An SQLite authorizer then allows only reading those two tables (and the query's own WITH tables): no writes, PRAGMA, ATTACH or schema tables. The query must start with SELECT, WITH or VALUES, be a single statement and be read-only; it is interrupted after 2 seconds (progress handler) and returns at most 1,000 rows (cells at most 64 KB). It runs on the history thread. `last_visit` and `visit_time` are local "YYYY-MM-DD HH:MM:SS" text so they read and compare easily (`date(visit_time)`, `datetime('now', 'localtime', '-7 days')`); `transition` is text. Results: numbers right-aligned, NULL shown as NULL, web and file addresses clickable (opened in the row's `profile` column's profile, else the history tab's), "Copy as CSV" (RFC 4180) through the bridge to the clipboard. SQLite's error messages are shown as they are. SQLite is built with up to 64 attached databases.
- 2026-10-23 (implementation, Clean up): "Clean up…" on the history page opens a dialog. Profile: the history tab's profile is preselected; another profile or "All profiles" (then each row shows its profile's colour and each profile uses its own keep list and suggestions). Time: last 24 hours, 7 days, 4 weeks or all time (preselected). Rows: every site with visits in that time, most visited first, with visits and last visit; a filter box narrows the list. Modes: "Keep selected sites, delete everything else" (default; preselected are the keep list and the 10 most visited sites, marked "suggested") and "Delete selected sites" (nothing preselected). The summary always says how many visits from how many sites will be deleted; the first click on Clean up asks again ("…? This cannot be undone."), the second deletes. Only visits inside the chosen time are deleted, so a site keeps its older visits; pages left without visits go. In keep mode the checked sites become the profile's keep list (`history.keep_list`, an advanced per-profile setting); sites on the old list that had no visits in the chosen time stay on it. Delete mode leaves the keep list as it is.
- 2026-10-23 (user's decision and implementation, Clear browsing data): CEF cannot do everything Chrome does; the user chose "closest possible". History and cookies are cleared by the chosen time range (cookies by their creation time). Site data (local storage, IndexedDB, Cache Storage, service workers, file systems…) is cleared through the DevTools protocol (`Storage.clearDataForOrigin`) for the sites visited in the time range plus the sites whose cookies were deleted, http and https. The HTTP cache is always cleared whole (CEF has no time range for it); the dialog says so. Download history follows with 014. A profile's cookies can be read only while a browser of that profile runs, so a hidden helper browser (about:blank) is used when the profile has no tab; if the cookie store does not answer within 10 seconds, the cookie count shows as unknown and clearing reports that cookies were not cleared (the rest is cleared). The dialog is on the history page ("Clear browsing data…"), opened also by History → Clear Browsing Data… (⇧⌘⌫, remappable) and a button in Settings → History (both open the history tab with `#clear`). Profile: this tab's profile (preselected), another, or all; time: last hour, 24 hours (preselected, as Chrome), 7 days, 4 weeks, all time. It shows visits and sites, cookies and cache size (per profile with "All profiles"), and asks again before clearing.
- 2026-10-23 (user's decision): history in profile export is postponed; bookmarks (011) and ad blocking (007) come first, so the browser can be used day to day. 013 stays in-progress until it is built.
- 2026-10-26 (user's decision): history in profile export is moved to its own spec, 022, so this spec can be finished without it. Its acceptance criterion moved there.
- 2026-10-26 (implementation, window choice of 010): an entry opened from the history page (or the SQL results) in a profile other than the history tab's goes to the window 010 chooses for that profile, as bookmarks do (011 notes). An entry in the history tab's own profile still opens in that tab, or next to it with ⌘.
- 2026-10-26 (implementation, favicons): history schema version 3 stores favicons as Chrome's History does: `favicons` (one row per icon URL, the 32 px PNG the tab downloaded, at most 64 KB) and `icon_mapping` (page URL → icon), so pages with the same icon share it. A page's icon is saved whenever a tab shows it, also when the next page has the same icon; not for pages that are not recorded (brouk://, never-record sites). A page without its own icon uses the icon of the most recently visited page of its host. Icons leave with their pages (deleting, Clean up, Clear browsing data, retention), except pages that are bookmarked (011), whose icons stay as in Chrome; an icon no page uses is removed. The history page shows the icons on site rows, page rows and visits (the first letter of the host when there is none); they are sent with each answer as data: URLs.
