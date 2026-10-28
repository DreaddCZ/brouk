# CEF update: manual test checklist

For every CEF update (`docs/cef-updates.md`). Patch updates: the steps marked
**(patch)**. Major updates: all steps. Use a test copy (separate build
folder, `--use-mock-keychain`), not the Brouk you work in.

1. **(patch)** Start Brouk. Expected: the session comes back, no crash; in DevTools → Console, `navigator.userAgent` shows the new Chromium version.
2. **(patch)** Open a few sites (a news site, YouTube with video and sound, a Google sign-in). Expected: pages render, video plays, sign-in works.
3. **(patch)** Open several tabs in two profiles and switch between them. Expected: each profile keeps its own sign-ins (cookies are separate).
4. Address bar: type, see suggestions, Enter; ⌘L, ⌘T, ⌘W, ⌘⇧T. Expected: as before.
5. Open Settings, History, Bookmarks, Passwords, Downloads, Help, the task manager. Expected: each page loads and its buttons work (they use the bridge).
6. Download a file, pause, resume, open it. Expected: as before.
7. Save a password on a test login form, sign out, fill it again. Expected: the save prompt and autofill work.
8. Ad blocking: open a site with ads. Expected: ads blocked, the shield shows a count.
9. DevTools: open docked and in a window; Elements, Console, Network, a request rule from 006. Expected: as before.
10. Permissions: camera, microphone, notifications and screen sharing on `docs/testing/pages/` (see its README). Expected: prompts appear and work.
11. Link routing: `open https://example.com` from Terminal, a link from another app. Expected: routed to the right profile.
12. Task manager → select a tab → End process. Expected: the tab shows "Aw, Snap!" with Reload.
13. Print a page and save it as PDF. Expected: the dialog and the PDF work.
14. **(patch)** Quit with ⌘Q. Expected: Brouk exits within a few seconds, without a crash report.
