# Manual test checklist: downloads (014)

Any site with downloadable files works; for big files use e.g.
https://speed.hetzner.de (100 MB test file) or a large release on GitHub.

## Saving

1. Download a file. → It goes to `~/Downloads` without asking (or the folder
   from Settings → Downloads).
2. Profile settings → **Download folder for this profile**: choose another
   folder. Download in a tab of that profile. → The file is saved there;
   other profiles still use the global folder.
3. Set that folder to a path that does not exist (edit the profile's
   settings.yaml) and download. → A save dialog asks where to save,
   starting in `~/Downloads`.
4. Settings → **Ask where to save each file** on. → Every download shows the
   save dialog, starting in the profile's folder. Turn it off again.
5. Download the same file twice. → The second is "name (1).ext". During a big
   download Finder shows a `.crdownload` file; cancel it → no file with the
   final name is left.

## Button and popup

6. After the first download of the run a downloads button appears right of
   the address bar; it shows a ring filling up while downloading and a dot
   when a download finished. The popup never opens by itself.
7. Click the button. → The popup lists the downloads of the last 24 hours of
   **all profiles**, each with its profile's colour dot, icon, name, site and
   status ("12.3 / 40.0 MB, 2 min left", "Done, …", "Failed — …",
   "Cancelled"). The dot on the button goes away.
8. In the popup: pause and resume a running download; cancel one; **Retry** a
   cancelled one; click a finished one (opens it); **Show in Finder**; the
   **…** menu: Copy Download Link, Remove from List, Delete File (moves it to
   the Trash). Drag a finished item to the Desktop or into Mail. → Each works.
9. **Show all downloads** opens the downloads page.

## Downloads page

10. ⌥⌘L or View → Downloads opens `brouk://downloads` as a tab: downloads of
    all profiles, newest first, grouped by day, with a profile filter and a
    search by file name and address. The same actions work there; progress
    updates live.
11. Delete or move a downloaded file in Finder, reload the page. → It shows
    "Deleted"; only Download again, Copy link and Remove remain.
12. **Clear all** removes every finished entry (of the filtered profile only
    when a profile is chosen); running downloads and all files stay.

## Safety, Dock, quitting

13. Finder → Get Info on a downloaded file. → "Where from" shows the address;
    an app downloaded this way gets the Gatekeeper check when first opened
    (quarantine set by Chromium itself).
14. While downloading, the Dock icon shows a progress bar and the number of
    running downloads; when one finishes, the Downloads stack in the Dock
    bounces.
15. A page that starts several downloads without a click (e.g. a script
    downloading three files) shows "wants to Download multiple files" with
    Allow / Block; Allow starts the held downloads and is remembered for the
    site (Site info → Automatic downloads).
16. Quit (⌘Q) while downloading. → "N downloads are in progress. Cancel them
    and quit?" with **Quit** and **Keep Downloading**.

## Profiles and history

17. History page → Clear browsing data → **Download history** (time range
    "All time"). → The downloads page is empty, the files are still in
    Finder.
18. Delete a profile that has downloads. → Its entries are gone from the
    page; the files stay.
