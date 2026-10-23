# 010 – Link routing and window picker

- **Status:** ready
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
- Popups opened by page scripts (e.g. sign-in popups that report back to the page): never; they always stay in the opener's profile, for the same reason.

**Link wrappers (safe links):** links from Outlook, Teams and other mail and chat tools often point to a security wrapper first: Microsoft Safe Links, Proofpoint URL Defense, Mimecast and others. The wrapper checks the link and then redirects to the real page. Routing always uses the **real URL**, never the wrapper's. Otherwise every wrapped link would match no rule, and the redirect would never change profile.

- **Known wrappers:**
  - Brouk has a built-in list of wrappers. Each entry says how to find the real URL.
  - The user can add their own wrapper hosts in settings: either the name of the URL parameter that holds the real URL, or "opaque".
- **Built-in decodable wrappers** (the real URL can be read from the link itself):
  - Microsoft Safe Links: `*.safelinks.protection.outlook.com` and the Teams Safe Links page (parameter `url`);
  - Proofpoint URL Defense: `urldefense.com`, `urldefense.proofpoint.com` (formats v1, v2 and v3);
  - Barracuda `linkprotect.cudasvc.com` (`a`);
  - Cisco `secure-web.cisco.com`;
  - Google `www.google.com/url` (`q` or `url`).
- **Built-in opaque wrappers** (the real URL is known only after loading): Mimecast `protect-*.mimecast.com`, `url.uk.m.mimecastprotect.com` and similar.
- **Decodable wrapper:**
  1. Brouk reads the real URL. Wrappers inside wrappers are unwrapped too, up to 5 levels.
  2. It routes by the real URL, choosing the profile and window, with the picker if needed.
  3. It then opens the **wrapper URL** in the chosen tab, so the company's check still runs and the click is logged.
  4. The wrapper redirects to the real page inside that tab, in the same profile, as for any redirect.
  - The decoded URL is treated as untrusted. If it is not a valid `http`/`https` URL, the wrapper is handled as opaque.
- **Opaque wrapper:**
  1. Brouk loads the wrapper in a **neutral, temporary context**: in memory only, no cookies or storage kept, deleted afterwards, and in no profile. No tab is shown.
  2. When the wrapper navigates or redirects to a host that is not a wrapper, Brouk stops that navigation and routes that URL as a link from the original source.
  3. If the wrapper does not leave its host within 5 seconds, it has a page of its own: a warning, a "continue" button, or an error. That page is shown in a small separate **"Checking link"** window. When the user continues and the page leaves the wrapper host, the window closes and the URL is routed.
  4. A download started in the neutral context is cancelled, and its URL is routed instead.
- **The picker and the "Always open…" checkbox use the real URL.** The picker shows "via Safe Links", "via Mimecast", etc. next to it.
- **Which links are unwrapped:** the same link opens that are routed (from other apps, new tabs, typed into a new tab). Clicking a wrapped link that navigates within the current tab is not routed, as before.

**When no rule matches:**

- Link from inside the browser: stays in the current profile.
- Link from another app: opens in the default profile's group of the most recently used window on the current screen.

**Destination window,** once the profile is known:

- Default profile: its group is always present in every window (see 003), so the link opens in the most recently used window on the current screen.
- The profile's group exists in exactly one window → open there.
- It exists in several windows → the user chooses the window.
- It exists in no window → open as a new group in the frontmost window.

**Picker:** one native dialog for choosing the profile and, when needed, the window.

- **Where it appears:**
  - centred on the current screen, in front of all windows;
  - for a link from another app, Brouk comes to the front first.
- **Top of the dialog:** the URL being opened (host in bold) and where it came from (the app's name, or "from <profile>" for a link inside the browser).
- **Step 1 – profile** (only when a rule lists several profiles):
  - the rule's profiles in the order of the rule, each with its colour, name and number key (1–9);
  - below the list, a checkbox **"Always open links matching `<pattern>` in this profile"**. If it is checked, the matched rule is changed to the chosen profile only, and the user can change it back in settings.
- **Step 2 – window** (only when the chosen profile's group is in several windows):
  - one entry per window, most recently used first;
  - each entry shows:
    - a **thumbnail** of the window's active tab;
    - the active tab's favicon and title;
    - the window's **profile groups** as coloured chips with tab counts (e.g. "Work 5 · Main 2"), with the chosen profile's group marked;
  - the last entry is always **"New window"**;
  - when step 1 was shown, Esc or ← goes back to it.
- **Keyboard:** number keys choose an entry at once; ↑/↓ plus Enter also work; Esc in the first step cancels, and the link is not opened. The mouse works too.
- **Thumbnails** come from the browser's own rendering, so they need no screen recording permission. A tab with nothing rendered yet (e.g. never shown since start) shows its favicon instead.
- **Chosen window on another Space:** depends on a setting.
  - **Switch to that Space** (default): macOS switches to the window's Space, as it does when the window is activated.
  - **Move the window here:** the window moves to the current Space.

## Acceptance criteria

- [ ] With a rule `companya.atlassian.net` → A, opening `https://companya.atlassian.net/browse/X-1` from another app opens it in profile A without a prompt.
- [ ] With a rule `portal.azure.com` → A, B, C, opening a matching link shows the picker with exactly A, B and C; a profile can be chosen with the keyboard alone.
- [ ] When several rules match, the most specific one is used (path prefix over host over subdomain wildcard).
- [ ] Redirects and navigation within a tab never change the tab's profile; a sign-in redirect to another domain completes in the original profile.
- [ ] Cmd-click and "open in new tab" are routed by the rules.
- [ ] A URL typed into a new tab is routed; a URL typed into an existing tab stays in that tab's profile.
- [ ] Popups opened by page scripts stay in the opener's profile, whatever the rules say.
- [ ] A link from another app that matches no rule opens in the default profile's group of the most recently used window on the current screen.
- [ ] A link from inside the browser that matches no rule stays in the current profile.
- [ ] The destination window follows the rules above (default profile → most recent window on the current screen; other profiles: one window → there; several → user chooses; none → new group in the frontmost window).
- [ ] Routing rules can be added, edited and deleted in settings.

**Link wrappers**
- [ ] A Safe Links link from Outlook or Teams to `https://companya.atlassian.net/browse/X-1`, with a rule `companya.atlassian.net` → A, opens in profile A without a prompt. The wrapper URL is loaded first in that tab, and the tab ends on the Jira page.
- [ ] The same works for Proofpoint URL Defense v1, v2 and v3, Barracuda, Cisco and Google redirect links, and for wrappers inside wrappers.
- [ ] A Mimecast link is resolved in the neutral context and opens in the profile the rules give for the final URL. That context keeps no cookies, and no data from it reaches any profile.
- [ ] An opaque wrapper that shows its own page (e.g. a warning) shows it in the "Checking link" window. After the user continues, the final URL is routed.
- [ ] The picker shows the real URL with "via <wrapper>". "Always open links matching …" changes the rule that matches the real URL, never one for the wrapper's host.
- [ ] A wrapper host added in settings, with a parameter name or as opaque, is unwrapped the same way.
- [ ] The picker shows the URL and where the link came from. In the window step, each window shows a thumbnail of its active tab, the active tab's favicon and title, and its profile groups with tab counts. "New window" is always the last entry.
- [ ] Choosing "New window" opens the link in a new window, in the chosen profile.
- [ ] Every picker step works with the keyboard alone (number keys, arrows plus Enter); Esc in the first step cancels without opening the link.
- [ ] Checking "Always open links matching … in this profile" changes the matched rule to that profile only; the next matching link opens without the picker.
- [ ] With the default setting, choosing a window on another Space switches to that Space; with "Move the window here", the window comes to the current Space.

## Out of scope

- Showing which Space a window is on, or listing Spaces by name (needs private macOS APIs).
- Choosing a tab position or a specific tab in the picker.
- Thumbnails of all tabs of a window (only the active tab).
- Rules based on anything other than the URL (e.g. the app the link came from).
- Skipping the company's link check: Brouk always loads the wrapper (or lets it run in the neutral context) and never opens the real URL without it.

## Configuration

| Setting | Default | Scope |
|---|---|---|
| Routing rules (pattern → one or more profiles) | none | global |
| When the chosen window is on another Space | switch to that Space (alternative: move the window here) | global |
| Link wrappers | built-in list (see Description) plus the user's own entries (host pattern + parameter name, or opaque) | global |

## Open questions

None.

## Notes

- 2026-10-21 (UI and profiles review): spec widened from "window picker" to link routing by profile; moved from idea/P2 to draft/P1. Rule format, routed sources, no-match behaviour and destination window agreed. Because the default profile's group is in every window, the earlier setting "no-match link from another app: most recent window or default profile" was dropped: both choices now lead to the default profile in the most recent window on the current screen.
- Displays: fully supported by public APIs on macOS and Windows.
- macOS Spaces: public APIs only tell whether a window is on the *active* Space and notify when the active Space changes. Listing all Spaces, knowing which Space a window is on, or moving a window to another Space needs private APIs (fragile, may break with macOS updates, not allowed in the Mac App Store).
- Windows virtual desktops: a public API reports a window's desktop and can move our own windows between desktops; listing desktops and their names is undocumented.
- Page thumbnails do not need screen recording permission: the browser renders its own content.
- 2026-10-23 (picker review with the user):
  - Window entries show a thumbnail of the active tab, its favicon and title, and the window's profile groups with tab counts.
  - "New window" is always the last entry.
  - A window on another Space: by default macOS switches to it; a setting moves the window to the current Space instead. That uses the public `NSWindowCollectionBehaviorMoveToActiveSpace`; verify it in the SDK headers.
  - The picker can save the choice by narrowing the matched rule to one profile.
  - Spec set to `ready`.
- 2026-10-23 (with the user): mail and chat tools wrap links in security redirects (Safe Links, URL Defense, Mimecast), so routing by the first URL would send every wrapped link to the default profile.
  - Agreed: route by the real URL.
  - For decodable wrappers, still load the wrapper in the chosen profile so the company check runs.
  - Resolve opaque wrappers in a neutral throwaway request context, showing their page only when it needs the user.
  - The decoders (Proofpoint v3 in particular) are pure core functions; cover them with unit tests using real sample links.
