# 013 – History

- **Status:** in-progress
- **Priority:** P1
- **Depends on:** 001, 003, 005, 010

## Goal

Record visited pages per profile, so the user can find them again, the address bar can suggest them, and closed tabs can be reopened. It works like Chrome's history, plus one view across all profiles.

## Description

### What is recorded

- History is stored **per profile** (005). Each profile's history is separate data. It is deleted with the profile and can be included in its export.
- **Recorded:**
  - Every top-level navigation to an `http`, `https` or `file` URL, with URL, page title, visit time and how the visit started (typed, link, bookmark, reload…).
  - Through a redirect chain, only the final URL is shown.
  - Each page's favicon, cached per profile. Bookmarks (011) use these icons too.
- **Not recorded:**
  - `brouk://` pages, `data:` and `blob:` URLs, error pages, and downloads.
  - Navigations inside frames other than the main frame.
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

As specified in 005, history can be included in a profile export, limited to the last N days or the last N pages. On import, it becomes the imported profile's history.

### Storage

- One **SQLite** database per profile: `profiles/<id>/history.db`. It holds visits, favicons and the downloads list (014), as Chrome's History file does.
- SQLite is public domain. It is added as a third-party library under the rules in `CLAUDE.md`, using the official amalgamation, and listed in `third_party/README.md`.
- The database is written in WAL mode, so a crash loses at most the last few visits.

## Acceptance criteria

**Recording**
- [ ] Visiting a page in profile A records it in A's history only; it is not suggested and not listed under profile B.
- [ ] Redirects show only the final URL; brouk://, data:, blob: and error pages are not recorded.
- [ ] Entries older than the retention limit (90 days by default) are removed automatically.

**History page**
- [ ] ⌘Y opens the history page grouped by exact host (companya.atlassian.net and companyb.atlassian.net are separate rows), with visit counts, last visit and profile colour; a row expands to its pages; it can be filtered by profile and sorted by last visit, visits or host.
- [ ] The "By day" toggle shows single visits grouped by day.
- [ ] Clean up in "keep selected" mode deletes every host in the chosen profile and time range except the checked ones, and shows the counts before confirming.
- [ ] Clean up preselects the profile's remembered keep list plus the 10 most visited hosts; the hosts kept become the new keep list.
- [ ] Clean up in "delete selected" mode deletes only the checked hosts.
- [ ] Search with plain words matches title or URL, all words required; the filters `site:`, `host:`, `title:`, `url:`, `after:`/`before:`/`on:` (absolute and relative), `visits:`, `profile:`, `type:`, phrases, `-` and `OR` work as described, alone and combined.
- [ ] The filter chips add the same filters as typing them.
- [ ] The SQL editor runs SELECT queries over `pages` and `visits` for all profiles and shows the results as a table, which can be copied as CSV.
- [ ] Any statement that would change data is rejected, and queries are stopped after 2 seconds or 1,000 rows.
- [ ] An entry opened from the history page opens in the profile it was visited in, in the window chosen by the rules of 010.
- [ ] A page, a site row, or a multi-selection of them can be deleted.

**Menu and closed tabs**
- [ ] ⇧⌘T reopens the last closed tab (or window) in its profile, at its previous position when possible, up to the last 10 tabs or windows of the current run.
- [ ] The History menu lists Recently Closed (with profile colours) and Recently Visited (active tab's profile).

**Address bar**
- [ ] Address bar suggestions from history come only from the active tab's profile; ⇧⌫ removes a history suggestion from that profile's history.

**Clearing and profiles**
- [ ] Clear Browsing Data clears the chosen data types, for the chosen profile or all profiles, within the chosen time range, and leaves other profiles untouched.
- [ ] Deleting a profile deletes its history.
- [ ] History can be included in a profile export, limited by days or number of pages, and is restored by the import. (Completes the history criterion of 005.)

## Out of scope

- History sync between devices.
- Chrome's "Journeys" / grouping of history by topic.
- Full-text search of page contents (search covers titles and URLs only).
- Automatic clean up of rarely visited sites (only the retention limit runs automatically).
- SQL that changes data, or SQL over other data (bookmarks, downloads, passwords).
- Session restore at startup and keeping closed tabs after quitting (003, left for testing).
- Import of history from other browsers (in line with 005).
- A private/incognito mode.

## Configuration

| Setting | Default | Scope |
|---|---|---|
| Keep history for | 90 days (alternatives: 30 days, 1 year, forever) | global |
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

