# 005 – Profiles

- **Status:** draft
- **Priority:** P1
- **Depends on:** 001, 004

## Goal

Keep separate work contexts (e.g. different clients, employers, personal) fully isolated, like Firefox profiles — while letting the user work with several of them side by side in the same window. Typical case: being logged in to several accounts of the same service at once (several Microsoft accounts for different Azure tenants, several Jira instances, several GitHub accounts, one context signed in with Google and another with Microsoft).

## Description

A profile is a completely separate set of browser data: cookies and logins, history, bookmarks, saved passwords, extensions (if ever added), settings and cache. The one deliberate exception is autofill data (addresses, contact details, payment cards), which is shared by all profiles (see 009).

- Profiles are **local only**: there is no Google or other account sign-in for a profile.
- Create, rename, delete and pick a colour/icon for a profile. Each profile has its own colour.
- A profile manager to choose which profile to open (optionally shown at startup).
- **Profiles are not tied to windows.** A window can hold tabs of several profiles. In each window, all tabs of one profile form exactly one tab group, labelled with the profile's name and colour; the group can be collapsed (see 003). Every tab belongs to a profile group, also in a window with a single profile.
- **Default profile:** a new window or tab opens in the default profile, unless the user picks a specific profile — for example with the "+" button of that profile's group. The default profile's group is always present in every window, even with no tabs; other profiles' groups exist only while they have tabs (see 003).
- **Moving a tab to another profile:** dragging a tab into another profile's group reopens its URL in that profile; the page reloads and its session and form state are lost. Before that, a dialog asks the user to continue or cancel.
- Window-level UI follows the active tab's profile: bookmarks bar, password filling, new tab page.
- Links are opened in the right profile by routing rules (see 010).
- Several profiles can run at the same time, in separate windows or in the same window.
- A profile can be launched directly, e.g. from the command line (`--profile <name>`) or a dock/desktop shortcut.
- Nothing leaks between profiles, apart from the shared autofill data.

## Acceptance criteria

- [ ] User can create, rename and delete profiles, and set a name and colour.
- [ ] Creating or using a profile never requires signing in to any online account.
- [ ] Logging into a site in profile A does not log in profile B, including when tabs of A and B are in the same window.
- [ ] History, bookmarks, passwords, extensions and settings are separate per profile.
- [ ] Autofill entries are shared: an entry saved in one profile is offered in every profile.
- [ ] Two or more profiles can be open simultaneously, both in separate windows and in the same window.
- [ ] In each window, a profile's tabs form exactly one group, shown with the profile's name and colour.
- [ ] A new window or tab opened without choosing a profile opens in the default profile; a group's "+" button opens a new tab in that group's profile.
- [ ] Dragging a tab into another profile's group first shows a "continue / cancel" dialog; on continue, the tab's URL reopens in the target profile; on cancel, nothing changes.
- [ ] The bookmarks bar and password filling use the active tab's profile.
- [ ] A specific profile can be started from the command line.
- [ ] The profile manager can be set to appear at startup or be skipped (opening a default profile).
- [ ] Deleting a profile removes all of its data, after confirmation.

## Out of scope

- Sync of profiles between devices.
- Online accounts tied to profiles (Chrome-style sign-in).
- Several groups of the same profile in one window.
- Firefox-style containers: profile groups in a shared window cover this need, with full isolation.

## Configuration

Default profile, show profile manager at startup (yes/no), per-profile name, colour and icon. Link routing rules are configured in 010.

## Open questions

- Import profiles/data from Chrome, Firefox or Safari?
- Temporary (throwaway) profiles?
- What happens to open tabs of a profile that is being deleted?

## Notes

- 2026-10-21 (decision in 001): profiles are our own implementation, not Chrome's profile system. Each profile is a separate CEF request context with its own storage directory. Because isolation is per request context rather than per window, profiles are not tied to windows the way they are in Chrome — which is what makes mixed-profile windows possible.
- 2026-10-21 (decision in 001): Chrome extensions are not supported; "extensions" above refers to our own extension mechanism, if one is ever added.
- 2026-10-21 (UI and profiles review): profiles are local only and can be mixed in one window, one group per profile per window. Containers are not needed. "Open links from other apps in which profile?" is answered by the routing rules in 010. Autofill is shared across profiles; passwords stay per profile. The default profile's group is always present in each window; every group has a "+" button for a new tab in its profile.
