# 013 – History

- **Status:** ready
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
  - each entry shows its profile's colour, favicon, title, domain and time;
  - entries are grouped by day, newest first.
- A **search** box matches titles and URLs.
- **Opening an entry** opens it in the profile it was visited in. When that is not the current tab's profile, the window is chosen as in 010. ⌘-click opens it in a new tab.
- **Actions:**
  - delete one entry, or several selected ones (⌘-click, ⇧-click);
  - "More from this site" (filters the list to the domain);
  - "Delete all from this site";
  - "Clear browsing data…".

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
- [ ] ⌘Y opens the history page; it lists all profiles' history grouped by day, with each entry's profile colour, and can be filtered by profile.
- [ ] Searching on the history page matches titles and URLs.
- [ ] An entry opened from the history page opens in the profile it was visited in, in the window chosen by the rules of 010.
- [ ] Single entries, a multi-selection, or all entries of one site can be deleted.

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
- Full-text search of page contents.
- Session restore at startup and keeping closed tabs after quitting (003, left for testing).
- Import of history from other browsers (in line with 005).
- A private/incognito mode.

## Configuration

| Setting | Default | Scope |
|---|---|---|
| Keep history for | 90 days (alternatives: 30 days, 1 year, forever) | global |
| History in address bar suggestions | on (already in 003) | global |

## Open questions

None.

## Notes

- 2026-10-21: placeholder created from the readiness review. Until this spec is ready, Claude Code may store visited URLs and titles per profile only as far as address bar suggestions (003) need it, in a storage module this spec will later extend.
- 2026-10-22 (history review with the user): agreed on Chrome-like history, 90-day retention, and the history page showing all profiles with a profile filter, like the bookmark manager. Clear browsing data, the History menu and ⇧⌘T reopening closed tabs (current run only) were taken from Chrome. Storage is SQLite per profile. Spec set to `ready`.
