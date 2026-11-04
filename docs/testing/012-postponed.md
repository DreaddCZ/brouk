# 012: postponed items — manual test checklist

Pull request: site permissions in settings, Picture-in-Picture, pinch zoom,
folder upload and file drops. Test on macOS and Windows. The test page is
`docs/testing/pages/media.html` (open it with File → Open or by dragging it
into a tab).

1. On a site that asks for the location (e.g. `docs/testing/pages/permissions.html`
   from a local server, or a maps site), choose **Allow**. Open Settings →
   **Site permissions**: the site is listed under the profile's tab with
   *Location: Allow*. *(Permissions can be reviewed from settings.)*
2. Change it to **Block**, reload the site: it is blocked without asking. Set it
   to **Ask (default)**: the site disappears from the list and asks again on
   its next request. *(… and revoked from settings.)*
3. Allow two permissions for a site, then **Reset** it in settings: the site
   is gone from the list, and the site information button in the address bar
   shows no permissions for it.
4. Switch to another profile's tab in Site permissions: the site from step 1
   is not listed there. *(A permission in profile A is not in profile B.)*
5. Type part of a site's name into **Search sites**: only matching sites stay.
6. Open the test page, click **Picture-in-Picture**: the clock video opens in a
   small window that stays above other apps; closing it (or clicking the
   button again) returns it to the page. Also try a YouTube video's own
   Picture-in-Picture button. *(Picture-in-Picture.)*
7. On the test page, **Folder**: choose a folder; the page lists its files with
   their paths. **Files**: choose several files; all are listed. *(Folder upload,
   multiple files.)*
8. Drag a file from Finder / File Explorer onto **Drop files here**: the page
   lists it. Drag one onto a web mail's attachment area, too. *(Files can be
   dropped onto pages.)*
9. On a trackpad, pinch out on a page: it magnifies; pinch in returns. The
   zoom level in the address bar does not change (as in Chrome). On Windows,
   with a precision touchpad, the same. *(Zoom by pinch.)*
