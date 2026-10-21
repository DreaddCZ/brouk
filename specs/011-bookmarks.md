# 011 – Bookmark manager

- **Status:** draft
- **Priority:** P1
- **Depends on:** 001, 003, 005

## Goal

Save, organise and quickly find frequently used pages, with a bookmark manager suited to work (many links, structured by project or client).

## Description

- Bookmark the current page with one click or shortcut; choose the folder and edit the title when saving.
- Organise bookmarks in nested folders; drag and drop to reorder and move.
- A bookmarks bar under the toolbar (can be hidden).
- A bookmark manager page to browse, search, edit, move, delete and bulk-edit bookmarks.
- Search bookmarks from the address bar and the command palette.
- Bookmarks are separate per profile (see 005).
- Import and export in the standard HTML bookmark format used by all major browsers.

Per the hybrid UI decision in 001, the bookmarks bar and the "add bookmark" popup are native UI, and the bookmark manager page is an HTML/TypeScript page rendered in CEF. CEF (Alloy style) has no bookmark system of its own, so storage and logic are our own implementation in the core.

## Acceptance criteria

- [ ] A page can be bookmarked via a toolbar button and a keyboard shortcut, choosing the folder and title.
- [ ] Bookmarks can be organised in nested folders and reordered by drag and drop (bar and manager page).
- [ ] The bookmarks bar can be shown or hidden.
- [ ] The manager page supports search, edit, move, delete and multi-select.
- [ ] Typing in the address bar or command palette suggests matching bookmarks.
- [ ] Each profile has its own bookmarks.
- [ ] Bookmarks can be imported from and exported to the standard HTML bookmark file.
- [ ] Deleting a bookmark or folder can be undone.

## Out of scope

- Sync between devices (possible later spec).
- Sharing bookmark folders with other people.

## Configuration

Show bookmarks bar (on/off), default folder for new bookmarks, include bookmarks in address bar suggestions (on/off).

## Open questions

- Tags in addition to folders?
- Firefox-style keywords (type `jira 123` to open a bookmarked URL with a parameter)?
- Direct import from installed Chrome, Firefox or Safari (beyond the HTML file)?
- Should bookmarks be shareable between profiles (e.g. a shared "work tools" folder), or strictly separate?
- Storage format (SQLite vs. a JSON file) — related to the storage decision in 004.
- Notes or descriptions per bookmark?

## Notes
