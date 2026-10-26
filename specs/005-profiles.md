# 005 – Profiles

- **Status:** in-progress
- **Priority:** P1
- **Depends on:** 001, 004

## Goal

Keep separate work contexts (e.g. different clients, employers, personal) fully isolated, like Firefox profiles — while letting the user work with several of them side by side in the same window. Typical case: being logged in to several accounts of the same service at once (several Microsoft accounts for different Azure tenants, several Jira instances, several GitHub accounts, one context signed in with Google and another with Microsoft).

## Description

A profile is a completely separate set of browser data: cookies and logins, history, saved passwords, extensions (if ever added), settings and cache. Two deliberate exceptions: autofill data (contact details, addresses, company details) is shared by all profiles (see 009), and bookmarks are one shared tree in which a bookmark or folder can be linked to a profile, so that it shows and opens only in that profile (see 011).

- Profiles are **local only**: there is no Google or other account sign-in for a profile.
- Create, rename, delete and pick a colour/icon for a profile. Each profile has its own colour.
- A profile manager to choose which profile to open (optionally shown at startup).
- **Profiles are not tied to windows.** A window can hold tabs of several profiles. In each window, all tabs of one profile form exactly one tab group, labelled with the profile's name and colour; the group can be collapsed (see 003). Every tab belongs to a profile group, also in a window with a single profile.
- **Default profile:** a new window or tab opens in the default profile, unless the user picks a specific profile — for example with the "+" button of that profile's group. The default profile's group is always present in every window, even with no tabs; other profiles' groups exist only while they have tabs (see 003).
- **Moving a tab to another profile:** dragging a tab into another profile's group reopens its URL in that profile; the page reloads and its session and form state are lost. Before that, a dialog asks the user to continue or cancel.
- Window-level UI follows the active tab's profile: bookmarks bar, password filling, new tab page.
- Links are opened in the right profile by routing rules (see 010).
- Several profiles can run at the same time, in separate windows or in the same window.
- A profile can be exported to a file and imported on another device. The export contains:
  - name, colour and icon, settings, and the bookmarks linked to the profile (see 011);
  - saved passwords, encrypted with a password the user enters when exporting (and again when importing);
  - history, if the user chooses (spec 022): when exporting, the user decides whether to include it and how much — e.g. the last N days or the last N visited pages.
  - It does not contain cookies or logged-in sessions, or cache: after importing, the user logs in to sites again.
- Deleting a profile that has open tabs is allowed: after the user confirms, its tabs close and its data is removed. The default profile cannot be deleted; the user first makes another profile the default.
- A profile can be launched directly, e.g. from the command line (`--profile <name>`) or a dock/desktop shortcut.
- Nothing leaks between profiles, apart from the shared autofill data and general (unlinked) bookmarks.

## Acceptance criteria

- [x] User can create, rename and delete profiles, and set a name and colour.
- [x] Creating or using a profile never requires signing in to any online account.
- [x] Logging into a site in profile A does not log in profile B, including when tabs of A and B are in the same window.
- [x] History, passwords, extensions and settings are separate per profile. (History after 013, passwords after 008; cookies, cache, storage and settings now.)
- [x] Bookmarks linked to a profile appear only in that profile; general bookmarks appear in all profiles. (after 011)
- [x] Autofill entries are shared: an entry saved in one profile is offered in every profile. (after 009)
- [x] Two or more profiles can be open simultaneously, both in separate windows and in the same window.
- [x] In each window, a profile's tabs form exactly one group, shown with the profile's name and colour.
- [x] A new window or tab opened without choosing a profile opens in the default profile; a group's "+" button opens a new tab in that group's profile.
- [x] Dragging a tab into another profile's group first shows a "continue / cancel" dialog; on continue, the tab's URL reopens in the target profile; on cancel, nothing changes.
- [x] The bookmarks bar and password filling use the active tab's profile. (after 011 and 008)
- [x] A specific profile can be started from the command line.
- [x] The profile manager can be set to appear at startup or be skipped (opening a default profile).
- [x] Deleting a profile removes all of its data, after confirmation; if it has open tabs, the confirmation says so and the tabs close. Bookmarks linked to it become general, and the confirmation says how many (after 011).
- [x] The default profile cannot be deleted while it is the default.
- [ ] A profile can be exported to a single file and imported on another device, keeping its name, colour, settings, bookmarks and passwords. (Name, colour and settings now; bookmarks after 011; passwords after 008.)
- [ ] Passwords in the export file are encrypted with a user-chosen export password; the file cannot be imported without it. (after 008)
- [ ] When exporting, the user chooses whether to include history and can limit it by age or by number of visited pages. (after 022)
- [x] The export contains no cookies, sessions or cache.

## Out of scope

- Sync of profiles between devices (manual export/import only for now).
- Importing data from Chrome, Firefox or Safari — not in the current phase.
- Temporary (throwaway) profiles.
- Online accounts tied to profiles (Chrome-style sign-in).
- Several groups of the same profile in one window.
- Firefox-style containers: profile groups in a shared window cover this need, with full isolation.

## Configuration

Default profile, show profile manager at startup (yes/no), per-profile name, colour and icon. Link routing rules are configured in 010.

## Open questions

None.

## Notes

- 2026-10-21 (readiness review): criteria that need passwords (008), bookmarks (011), history (013) or autofill (009) are marked "(after NNN)" and are completed when those specs are built; the rest of this spec can be implemented now. The export file format must be versioned and extensible, so those parts can be added later without breaking older exports.
- 2026-10-21 (implementation, 001): a profile's data is in two folders: `profiles/<id>/` (our files) and `web/<id>/` (Chromium's cookies, cache, storage). Deleting a profile removes both. Until this spec is implemented, the single profile has the id `default`.
- 2026-10-21 (readiness review): each profile has a stable internal ID used for its storage folder (004); the name is only a display label and can change freely.
- 2026-10-21 (decision in 001): profiles are our own implementation, not Chrome's profile system. Each profile is a separate CEF request context with its own storage directory. Because isolation is per request context rather than per window, profiles are not tied to windows the way they are in Chrome — which is what makes mixed-profile windows possible.
- 2026-10-21 (decision in 001): Chrome extensions are not supported; "extensions" above refers to our own extension mechanism, if one is ever added.
- 2026-10-21 (UI and profiles review): profiles are local only and can be mixed in one window, one group per profile per window. Containers are not needed. "Open links from other apps in which profile?" is answered by the routing rules in 010. Autofill is shared across profiles; passwords stay per profile. The default profile's group is always present in each window; every group has a "+" button for a new tab in its profile.
- 2026-10-21 (profiles review): no import from other browsers in the current phase; no temporary profiles; profiles can be exported/imported between devices; deleting a profile with open tabs is allowed after confirmation. The default profile cannot be deleted, because its group must exist in every window (003).
- 2026-10-21 (profiles review): export contents agreed — settings, bookmarks, encrypted passwords, optional history limited by age or count; no cookies, sessions or cache. Spec set to `ready`.
- 2026-10-22 (implementation, part 1): the first profile has the id `default` and is named "Main" (renameable); new profiles get ids like `p-1a2b3c4d`. Names are unique (ignoring case). Profile colours are Chrome's nine tab group colours. Each profile gets its own CEF request context (`web/<id>/`), created when first used. Tabs opened from a page (links, popups, popup windows, "Open Link in New Window") stay in the opener's profile; Cmd+T, Cmd+N and links from other apps use the default profile, as specified. The Profiles menu (like Chrome's) opens a new tab in a profile, with Option a new window, and has "New Profile…" (name and colour); renaming, recolouring and deleting come with the profile manager. `--profile Work` and `--profile=Work` (name or id) both work, also for a second launch while Brouk runs; an unknown name opens the default profile.
- 2026-10-22 (implementation): collapsing a group follows Chrome: if it holds the active tab, the nearest visible tab outside the group (to the right first) becomes active; a group whose tabs are the only visible ones does not collapse. Ctrl+Tab skips collapsed groups; activating a tab expands its group.
- 2026-10-22 (implementation): the browser window is no longer moved by macOS itself: in the title bar's height a drag on a tab moved the window instead of the tab. Empty space in the tab strip and toolbar moves the window instead (double-click zooms), as in Chrome.
- 2026-10-22 (implementation, part 2): the profile manager is its own window (like Chrome's profile picker) showing the internal page `brouk://profiles`, opened from Profiles → Manage Profiles…, or instead of a browser window at startup when "Show this window when Brouk starts" is on (not when the launch names URLs or a profile). Opening a profile from it at startup closes it, as Chrome does. It lists profiles with their open tabs and can create, rename, recolour, make default, delete and open them. Deleting asks in a native dialog (saying how many tabs will close), closes the profile's tabs without asking their pages, and removes `profiles/<id>/` at once; Chromium's data in `web/<id>/` is removed at the next start, since Chromium may still use it. A profile icon (mentioned in the description) is not offered yet; the acceptance criteria ask for name and colour.
- 2026-10-22 (implementation): internal pages are TypeScript compiled by `tsc` into the app bundle and served from `brouk://<page>/` in a separate in-memory request context, with a strict Content-Security-Policy. They talk to the browser only through `web/shared/bridge.ts` (CEF's message router), which is added to brouk:// pages only; the browser accepts a page's messages only from that page's own URL and checks every field. Web tabs cannot open brouk:// URLs.
- 2026-10-22 (implementation, part 3): export and import from the profile manager ("Export…" per profile, "Import…"). The file (`.brouk-profile`) is YAML with `format: brouk-profile`, `version: 1`, the export time and the profile's settings (name, colour, ...); it contains no cookies, sessions or cache. Bookmarks (011), encrypted passwords (008) and optional history (013) will be added as further sections; `version` is raised only for changes older versions cannot read, and such files are refused with "exported by a newer version". Sections a version cannot import are listed after the import. An imported profile gets a new id; if its name exists, " 2", " 3", ... is added.
- 2026-10-22 (bookmarks review, 011, approved by the user): bookmarks are no longer separate per profile. There is one shared tree; a bookmark or folder can be linked to a profile and then shows and opens only in that profile. The profile export carries the linked bookmarks; deleting a profile makes its linked bookmarks general.
- 2026-10-23 (passwords and autofill review, 008/009): each profile chooses its password source, built-in or Bitwarden. A Bitwarden profile's passwords live in Bitwarden and are not exported, only the source setting is. Autofill has no payment cards.
- 2026-10-26 (user's decision): history in profile export is its own spec, 022; the criterion waits for it.
