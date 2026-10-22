# 014 – Downloads

- **Status:** ready
- **Priority:** P2
- **Depends on:** 001, 003, 005, 010, 012, 013

## Goal

Manage downloaded files: see progress, open, show in Finder, pause, resume and cancel, and find past downloads. It works like Chrome, with one view across all profiles and an optional download folder per profile.

## Description

### Where files go

- Files are saved to the **download folder** without asking, as in Chrome:
  - by default `~/Downloads`;
  - **each profile can override it** with its own folder in its settings (e.g. `~/Work/ClientA/Downloads`).
- If the profile's folder does not exist or cannot be written, the download asks where to save, starting in `~/Downloads`.
- "Ask where to save each file" (setting from 012, off by default) shows the save dialog for every download. The dialog starts in the profile's download folder.
- **Name clashes:** if a file with the same name exists, " (1)", " (2)", … is added.
- **While downloading:**
  - the file is written under a temporary name and renamed when complete, as in Chrome;
  - an interrupted download leaves no file with the final name.
- "Save Link As…", "Save Image As…" and File → Save Page As… (⌘S) always ask where to save.

### Downloads button

- A **downloads button** appears in the toolbar when the first download of the run starts, and stays until the app quits.
  - It shows a progress ring while anything is downloading.
  - It shows a dot when a download has finished that the user has not looked at yet.
- **The popup does not open by itself** when a download starts or finishes. The button is the only signal.
- Clicking the button opens a popup with the downloads of the last 24 hours from **all profiles**. Each item shows:
  - its profile's colour, an icon, the file name, the site, and a status ("12.3 / 40 MB, 2 min left", "Done", "Failed — network error", "Cancelled").
- **Actions per item:**
  - click to open the file;
  - pause, resume, cancel, retry;
  - show in Finder;
  - copy download link;
  - remove from list;
  - delete the file.
- A finished item can be **dragged** from the popup to Finder or another app.
- The popup has a "Show all downloads" link to the downloads page.

### Downloads page

- An internal HTML page, `brouk://downloads`, opened with ⌥⌘L, View → Downloads, or from the popup.
- It lists the downloads of **all profiles**, newest first, with a **profile filter** (All / one profile) and a **search** box (file name and URL).
- Items have the same actions as in the popup. A file that was moved or deleted is shown as "Deleted", and only the URL can be copied or downloaded again.
- "Clear all" removes every entry except running downloads from the list. The files stay. With a profile filter set, it clears only that profile's entries.

### Dock

While files are downloading, the Dock icon shows a progress bar and the number of active downloads, as in Chrome. When a download finishes, the Downloads stack in the Dock bounces, as Chrome makes it do.

### Safety

- Downloaded files get macOS's **quarantine attribute**, with the source URL. Gatekeeper then checks apps and installers when they are first opened, and Finder's "Where from" shows the source.
- **Not available:** Google Safe Browsing (CEF does not include it), so there is no malware scan or "dangerous file" verdict.
- Other Chrome download protections that CEF keeps are left on, e.g. blocking insecure (http) downloads started from https pages. Do not re-implement ones CEF lacks.
- **Several downloads from one page:** a page that starts several downloads without a click in between asks for the "Automatic downloads" permission, as in Chrome. The permission is stored per site and per profile (012).

### Quitting

Quitting while downloads are running asks: "N downloads are in progress. Cancel them and quit?" — with **Quit** and **Keep downloading** buttons.

### Storage and profiles

- The downloads list is stored per profile in its history database (013). Clear Browsing Data → "Download history" clears it, but not the files.
- Deleting a profile removes its downloads list; downloaded files are not deleted.
- The downloads list is not part of a profile export.

This replaces the interim download behaviour and notice in 012.

## Acceptance criteria

**Saving**
- [ ] Downloads are saved without asking to the active profile's download folder: `~/Downloads`, or the profile's own folder when one is set.
- [ ] If the profile's folder is missing or not writable, a save dialog asks where to save.
- [ ] "Ask where to save each file" shows a save dialog starting in the profile's download folder.
- [ ] A clashing file name gets " (1)", " (2)", …; a failed or cancelled download leaves no file under the final name.

**Downloads button and popup**
- [ ] The downloads button appears at the first download and shows progress; the popup does not open by itself.
- [ ] The popup lists the last 24 hours of downloads from all profiles, with each item's profile colour.
- [ ] From the popup and the page, a download can be opened, shown in Finder, paused, resumed, cancelled, retried, removed from the list, or deleted from disk; a finished item can be dragged out.

**Downloads page**
- [ ] ⌥⌘L opens the downloads page, listing downloads of all profiles, filterable by profile and searchable by file name and URL.
- [ ] A file deleted or moved outside the browser shows as "Deleted" on the page.

**Safety, Dock and quitting**
- [ ] Downloaded files carry the quarantine attribute with the source URL (visible as "Where from" in Finder's Get Info).
- [ ] The Dock icon shows progress and the number of active downloads.
- [ ] Quitting with running downloads asks for confirmation.

**Profiles**
- [ ] Clearing download history (013) or deleting a profile removes list entries but not the files.

## Out of scope

- Malware scanning (Safe Browsing or similar).
- A built-in download accelerator, scheduling or torrent support.
- Resuming downloads after the app was quit. Retry starts again (resumed when the server supports ranges, as far as CEF allows).
- Automatically opening certain file types after download.
- Including the downloads list in a profile export.

## Configuration

| Setting | Default | Scope |
|---|---|---|
| Download folder | `~/Downloads` (already in 012) | global |
| Download folder override | none | per profile |
| Ask where to save each file | off (already in 012) | global |

## Open questions

None.

## Notes

- 2026-10-21: placeholder created from the readiness review; the user will specify downloads later.
- 2026-10-22 (downloads review with the user):
  - Chrome-like downloads, with these choices:
    - the downloads page and popup show all profiles with a profile filter;
    - the folder is `~/Downloads`, with an optional override per profile;
    - the downloads popup never opens by itself — the toolbar button with its progress ring is the signal.
  - Taken from Chrome: the Dock progress, the quit confirmation and the automatic-downloads permission.
  - Not available: Safe Browsing (not in CEF).
  - Spec set to `ready`.
- Check in the CEF headers before implementing: `CefDownloadHandler` (`CanDownload`, `OnBeforeDownload`, `OnDownloadUpdated`), `CefDownloadItem`, `CefDownloadItemCallback` (pause/resume/cancel). Verify whether CEF already sets the quarantine attribute on macOS before adding our own.
