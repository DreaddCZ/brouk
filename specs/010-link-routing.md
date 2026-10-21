# 010 – Link routing and window picker

- **Status:** draft
- **Priority:** P1
- **Depends on:** 001, 003, 005

## Goal

Open every link in the right profile and window without manual copying between profiles: automatically when a rule says where a link belongs, and with a quick choice when there are several valid options. This is a main reason for building this browser — neither Chrome nor Firefox handles links across profiles this way.

## Description

The browser keeps a live model of all its windows and tabs, including which display each window is on and whether it is on the current desktop/Space.

**Routing rules** map URL patterns to one or more profiles:

- Patterns: a host (`companya.atlassian.net`), a subdomain wildcard (`*.atlassian.net`) or a path prefix (`github.com/company-a/*`). Path prefixes matter for services where all accounts share one host, such as GitHub.
- The most specific matching rule wins.
- A rule with **one** profile opens the link in that profile without asking.
- A rule with **several** profiles shows the picker, listing only those profiles (e.g. `portal.azure.com` → A, B or C).
- Only a few rules are expected, for the most used sites: Atlassian products, cloud providers, git providers and similar.

**Which link opens are routed:**

- Links opened from other apps: always.
- New tabs and windows the user opens from inside the browser (Cmd-click, middle-click, "open in new tab", links that open a new tab): yes.
- URLs typed in the address bar: only when typed into a new tab.
- Navigation within a tab, including redirects: never. Otherwise sign-in flows (Microsoft, Google, GitHub) would jump to another profile and break.

**When no rule matches:**

- Link from inside the browser: stays in the current profile.
- Link from another app: opens either in the most recently used window on the current screen, or in the default profile — chosen in settings.

**Destination window,** once the profile is known:

- The profile's group exists in exactly one window → open there.
- It exists in several windows → the user chooses the window.
- It exists in no window → open as a new group in the frontmost window.

**Picker:** one dialog for choosing the profile and, when needed, the window. Fully keyboard-driven (number keys or arrows plus Enter).

## Acceptance criteria

- [ ] With a rule `companya.atlassian.net` → A, opening `https://companya.atlassian.net/browse/X-1` from another app opens it in profile A without a prompt.
- [ ] With a rule `portal.azure.com` → A, B, C, opening a matching link shows the picker with exactly A, B and C; a profile can be chosen with the keyboard alone.
- [ ] When several rules match, the most specific one is used (path prefix over host over subdomain wildcard).
- [ ] Redirects and navigation within a tab never change the tab's profile; a sign-in redirect to another domain completes in the original profile.
- [ ] Cmd-click and "open in new tab" are routed by the rules.
- [ ] A URL typed into a new tab is routed; a URL typed into an existing tab stays in that tab's profile.
- [ ] A link from another app that matches no rule opens according to the setting (most recent window on the current screen, or the default profile).
- [ ] A link from inside the browser that matches no rule stays in the current profile.
- [ ] The destination window follows the rules above (one window → there; several → user chooses; none → new group in the frontmost window).
- [ ] Routing rules can be added, edited and deleted in settings.

## Out of scope

To be defined.

## Configuration

- Routing rules (pattern → one or more profiles).
- Link from another app with no matching rule: most recent window on the current screen (default) or default profile.

## Open questions

- No-match link from another app opened in "the most recent window on the current screen": in which profile — the profile of that window's active tab?
- Popups opened by page scripts (e.g. sign-in popups that report back to the page): proposed to always stay in the opener's profile, never routed. Confirm.
- What does the picker show: window titles, tab lists, page thumbnails, profile colour, display and Space?
- Can the user also choose "new window" from the picker?
- After choosing a window on another Space: switch to that Space, or move the window to the current one?

## Notes

- 2026-10-21 (UI and profiles review): spec widened from "window picker" to link routing by profile; moved from idea/P2 to draft/P1. Rule format, routed sources, no-match behaviour and destination window agreed.
- Displays: fully supported by public APIs on macOS and Windows.
- macOS Spaces: public APIs only tell whether a window is on the *active* Space and notify when the active Space changes. Listing all Spaces, knowing which Space a window is on, or moving a window to another Space needs private APIs (fragile, may break with macOS updates, not allowed in the Mac App Store).
- Windows virtual desktops: a public API reports a window's desktop and can move our own windows between desktops; listing desktops and their names is undocumented.
- Page thumbnails do not need screen recording permission: the browser renders its own content.
