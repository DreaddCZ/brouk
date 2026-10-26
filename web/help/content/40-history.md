## History {#history}

Brouk remembers the pages you visit, **per profile**: a page visited in one profile is never suggested in another.

- Recorded: web and file pages, with title, time and how you got there (typed, link, bookmark, reload). Of a redirect chain, only the final page is kept — also when a page forwards by itself.
- Not recorded: Brouk's own pages, error pages, downloads, frames, and sites on your [never-record list](#history-never-record).
- Kept for **90 days** by default (Settings → [History](#settings-history): 30 days, 1 year or forever).
- Each page's site icon is kept with the history, and bookmarks use it.

### History page {#history-page}

<kbd>⌘Y</kbd> (History → Show Full History) opens the history of **all profiles** as a tab:

- **By site** (default): one row per exact host and profile — `companya.atlassian.net` and `companyb.atlassian.net` are separate rows — with visits, pages and last visit. Sort by last visit, visits or site; click a row to see its pages.
- **By day:** single visits under Today, Yesterday and dates.
- **Profile** filter: all profiles or one; each row shows its profile's colour.
- **Open** a page by clicking its title — in its own profile. If that profile's tabs are in another window, it opens there; if in several, Brouk asks which one. <kbd>⌘</kbd>-click opens a background tab.
- **Delete** with ✕ on a row, or tick several rows (<kbd>⇧</kbd>-click ticks a range) and delete them together. Deleting a site deletes all its pages.

### Searching history {#history-search}

Plain words match the title or the address; all words must match; case and accents don't matter. Add filters, alone or combined:

| Filter | Meaning |
|---|---|
| `"exact phrase"` | phrase in the title or address |
| `-word`, `-site:example.com` | leave out |
| `jira OR confluence` | either word (OR binds closer than the spaces: `a b OR c` = a and (b or c)) |
| `site:atlassian.net` | that host or its subdomains (`*.` allowed) |
| `host:companya.atlassian.net` | exactly that host |
| `title:sprint` | in the title only |
| `url:/browse/` | in the address only |
| `after:2026-10-01`, `before:…`, `on:…` | visit date; also `today`, `yesterday`, or a time ago: `7d`, `2w`, `3m`, `1y` |
| `visits:>3` | pages with more than 3 visits (also `<`, `=`, `>=`, `<=`) |
| `profile:Work` | visits in that profile |
| `type:typed` | how the visit started: `typed`, `link`, `bookmark`, `reload`, `other` |

The chips under the box (Site, Date, Profile, Visits, Type) add the same filters for you. Anything the search doesn't understand is underlined with the reason; the rest still applies. Examples:

- `site:atlassian.net after:7d -title:dashboard`
- `"release notes" OR changelog visits:>2`

### SQL editor {#history-sql}

The **SQL** button replaces the search with a read-only SQL editor (SQLite) over the history of all profiles. Two tables:

- `pages(profile, host, url, title, visit_count, typed_count, last_visit)`
- `visits(profile, host, url, title, visit_time, transition)`

`profile` is the profile's name; times are local text (`2026-10-26 14:05:00`), `transition` is `typed`, `link`, `bookmark`, `reload` or `other`. Examples:

```sql
-- Most visited sites this month, per profile
SELECT profile, host, count(*) AS visits FROM visits
WHERE visit_time >= datetime('now', 'localtime', '-30 days')
GROUP BY profile, host ORDER BY visits DESC LIMIT 20;

-- Jira issues I opened more than 3 times
SELECT title, url, visit_count FROM pages
WHERE url LIKE '%/browse/%' AND visit_count > 3 ORDER BY last_visit DESC;
```

Only reading is allowed: anything that would change data is refused. A query stops after 2 seconds and shows at most 1,000 rows. Addresses in the results are clickable; **Copy as CSV** copies the table.

### Clean up {#history-clean-up}

**Clean up…** on the history page removes clutter but keeps the sites you care about:

1. Choose the **profile** (or all profiles) and the **time** (last 24 hours, 7 days, 4 weeks or all time).
2. Choose the mode:
   - **Keep selected sites, delete everything else** (default): your keep list from last time and the 10 most visited sites (marked "suggested") are ticked; tick or untick freely.
   - **Delete selected sites.**
3. Brouk says how many visits from how many sites will go, and asks once more. Deleting can't be undone.

In keep mode, the ticked sites become the profile's keep list for next time. Only visits in the chosen time are deleted.

### Clear browsing data {#history-clear-data}

**Clear browsing data…** (<kbd>⇧⌘⌫</kbd>, also on the history page and in Settings → History) clears, for one profile or all, within a time range (last hour to all time):

- **Browsing history**;
- **Download history** — the list only, the files stay;
- **Cookies and site data** — this signs the profile out of sites;
- **Cached images and files** — always the whole cache, whatever the time range.

It shows how much will be cleared before it does. Bookmarked sites keep their icons.

### History menu {#history-menu}

- Back, Forward, Home (<kbd>⇧⌘H</kbd>, the new tab page).
- **Reopen Closed Tab** (<kbd>⇧⌘T</kbd>): the last closed tab — or window — goes back where it was, in its profile, pinned if it was. Brouk remembers the last 10 closed tabs and windows until you quit.
- **Recently Closed** and **Recently Visited** (the last pages of the active tab's profile).
- Show Full History (<kbd>⌘Y</kbd>).

### Never record these sites {#history-never-record}

Settings → History → **Never record these sites**: [URL patterns](#url-patterns) separated by commas, e.g. `login.microsoftonline.com, *.okta.com`. Their pages are never recorded in any profile, and pages already in the history are removed.
