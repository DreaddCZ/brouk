# 027 – Utilities for testers in developer tools

- **Status:** done
- **Priority:** P2
- **Depends on:** 006 (developer tools dock and the tools page)

## Goal

Testers of web apps keep converting small values by hand or in random web sites: decoding a Base64 string from a header, reading a Unix timestamp from an API response, URL-encoding a parameter, unescaping HTML. Pasting tokens, IDs and customer data into third-party sites leaks them. Brouk should have these converters at hand, in the dev tools, working offline, so values never leave the computer. Testers also need to see how a site behaves for a user elsewhere — another time zone, language or location — without changing the whole computer's settings.

## Description

A new tab **Utilities** in the dev tools dock (006), next to Site data, Tokens, Requests and Rules. It is part of the tools page (`brouk://tools/#utilities`) and, like the other tools, works for the current tab. The converters need nothing from the tab, so they also work with no page loaded; only Tab environment (below) acts on the tab. Brouk's own internal pages have no dock (their DevTools opens in a window of its own, 006), so Utilities are not offered there.

The utilities live only in the dock; there is no separate window. **Utilities** in the menu (macOS: View › Developer; Windows: the app menu's More tools) and the command palette (003), which has an entry per utility ("Base64", "Unix time", "URL encoding"…), turn on the current tab's dev tools if they are off (as the dev tools shortcuts do, 006) and show the Utilities tab at that utility.

The tab lists the utilities on the left; the chosen one is shown on the right. Each utility has an **input** and an **output** field, converts **as you type** (no Convert button), shows a clear message for input it cannot convert (e.g. "Not valid Base64: unexpected character at position 17") instead of a wrong result, and has **Copy** for the output and **Swap** (output becomes input, direction reversed) where a utility works both ways. Text is handled as UTF-8.

### Utilities

1. **Base64** — encode and decode text; standard and URL-safe alphabet (`-_`, without padding), detected when decoding. A decoded result that is not valid text is shown as hex, with its length in bytes. A file can be dropped or chosen to get its Base64 (and a `data:` URL with its type).
2. **Unix time** — a timestamp in seconds, milliseconds or microseconds (the unit is detected from the size and can be overridden) is shown as local time, UTC, ISO 8601 and relative ("3 h ago", "in 12 min"); and the other way: a date and time (or ISO 8601 text) gives the timestamp in seconds and milliseconds. **Now** fills the current time. The local time zone is shown; another time zone can be chosen.
3. **URL encoding** — encode and decode percent-encoding (`%20`): as a URL component (everything special encoded, like `encodeURIComponent`), as a whole URL (keeps `:/?#&=`), and as form data (space as `+`). Decoding reports invalid sequences.
4. **URL parser** — a URL split into scheme, user, host, port, path, query parameters (a table, decoded, editable, add/remove) and fragment; editing a part rebuilds the URL.
5. **HTML entities** — encode special characters (`<` → `&lt;`, optionally all non-ASCII as `&#…;`) and decode named, decimal and hex entities.
6. **Hashes** — SHA-1, SHA-256, SHA-384, SHA-512 of text or of a dropped file, as hex and Base64; **HMAC** with a key for the same algorithms.
7. **UUID** — generate UUID v4 and v7 (one or many, upper/lower case, with or without hyphens); paste a UUID to see its version and, for v7, its time.
8. **JSON / string escaping** — escape text as a JSON string literal and back (e.g. to put a JSON body into another JSON field); format and minify JSON, with the error position for invalid JSON.
9. **Number bases** — a number in decimal, hex, octal or binary shown in all four; text to hex bytes and back.
10. **Regular expressions** — a pattern with flags (`g`, `i`, `m`, `s`, `u`) tested against a text: matches highlighted in the text and listed with their position and capture groups (numbered and named); an optional replacement shows the result. Uses the browser's (JavaScript) syntax, which is said on the page, as other languages' syntax differs in details.
11. **Cron expressions** — a cron expression explained in words ("At 08:30, Monday to Friday") with its next 5 run times in local time; standard 5-field cron and 6 fields with seconds first (as in Spring and Quartz), plus `@daily` and similar shortcuts. Invalid fields are pointed out.
12. **Colours** — a colour as hex (`#rgb`, `#rrggbb`, with alpha), `rgb()`/`rgba()`, `hsl()`/`hsla()` or a CSS colour name is shown in all of these forms, with a preview swatch and a colour picker.

### Tab environment

Unlike the converters, **Tab environment** changes how the browser presents itself to the **current tab**, to test how a site behaves elsewhere. It is the last entry of the list; each override can be switched on and off on its own:

- **Time zone** — any IANA time zone (searchable, e.g. `Asia/Tokyo`); dates, `Date` and `Intl` in the page use it.
- **Language** — one or more languages in order (e.g. `de-DE, de, en`): sent as the `Accept-Language` header, returned by `navigator.language` / `navigator.languages`, and used as the default locale of `Intl` and date formatting — so both server-side and in-page language detection see it.
- **Location** — latitude, longitude and accuracy, or a preset city (as Chrome's DevTools Sensors offer: Berlin, London, Moscow, Mumbai, San Francisco, Shanghai, São Paulo, Tokyo, plus Prague), or **Location unavailable**. The site still needs its location permission (012); the override only replaces the position it gets.
- **User agent** — a preset (Chrome on Windows / macOS / Android / iPhone, Safari, Firefox, Edge) or custom text; client hints are adjusted to match.
- **Colour scheme and motion** — `prefers-color-scheme` light or dark, and `prefers-reduced-motion`, independent of the system setting.

Rules:

- Overrides apply only to the current tab, only while its dev tools are on. They stay across reloads and navigations in that tab; tabs and popups opened from it are not affected.
- Turning the tab's dev tools off (or closing the tab) ends all overrides; the page then sees the real values again (after a reload where the page has already read them).
- While any override is on, the dock shows a bar above its tabs — "This tab uses: time zone Asia/Tokyo, language de-DE" — with **Reset** and **Reload**, so changed behaviour is never invisible, like request rules (006). A change that the page has already read offers **Reload to apply**.
- The overrides chosen are remembered as the Tab environment's options (so the same test can be repeated), but they are applied to a tab only when switched on there.

### Shared behaviour

- **From the page:** selected text can be sent to a utility from the page's right-click menu (**Decode with › Base64 / URL / Unix time / HTML entities**, shown whenever text is selected); the tab's dev tools turn on if they are off, and the utility opens in the dock with the text as input.
- Utilities remember their last choice of options (e.g. URL-safe, time zone), **not** their input or output: values may be tokens or personal data and are gone when the dock closes.
- The converters run inside the tools page; nothing is sent over the network or written to disk.
- Keyboard: every control can be reached with the keyboard; the utility list can be filtered by typing.

## Acceptance criteria

- [x] The dev tools dock has a **Utilities** tab; it works with any web page and with an empty tab (internal pages have no dock, 006).
- [x] The menu item Utilities and the command palette entries turn on the current tab's dev tools if needed and show the Utilities tab (at the chosen utility).
- [x] Each utility in the list converts as the input changes, in both directions where it has two, with Copy and Swap.
- [x] Base64: `Žluťoučký kůň` encodes to `xb1sdcWlb3XEjWvDvSBrxa/FiA==` and back; URL-safe input decodes; invalid input shows the position of the problem; binary output is shown as hex; a dropped file gives its Base64 and data URL.
- [x] Unix time: `1730000000` and `1730000000000` both show 2024-10-27 03:33:20 UTC, with local time, ISO 8601 and relative time; a date gives the timestamp back; Now works; another time zone can be chosen.
- [x] URL encoding: `a b&c=d/é` encodes as a component to `a%20b%26c%3Dd%2F%C3%A9`, as form data with `+` for the space, and decodes back; `%E9` alone or `%G1` is reported as invalid.
- [x] URL parser: query parameters are listed decoded; editing one or adding/removing one updates the URL.
- [x] HTML entities: `<a href="x">&</a>` encodes and decodes; `&eacute;`, `&#233;` and `&#xE9;` decode to `é`.
- [x] Hashes and HMAC give the same results as `shasum` / `openssl` for text and for a file.
- [x] UUIDs generated as v4 and v7 are valid; a pasted v7 UUID shows its time.
- [x] JSON escaping round-trips text with quotes, backslashes, newlines and non-ASCII characters; invalid JSON shows the error position.
- [x] Number bases convert between decimal, hex, octal and binary, also for numbers above 2^53.
- [x] Regular expressions: `(?<y>\d{4})-(\d{2})` with `g` on `2024-10 and 2025-01` highlights two matches and lists both groups (`y` named); a replacement `$2/$<y>` gives `10/2024 and 01/2025`; an invalid pattern shows the browser's error.
- [x] Cron: `30 8 * * 1-5` is explained as at 08:30, Monday to Friday, with the next 5 run times; `0 */15 * * * *` (6 fields) runs every 15 minutes; `@daily` works; `61 * * * *` points out the minute field.
- [x] Colours: `#3366cc`, `rgb(51, 102, 204)` and `hsl(220, 60%, 50%)` are recognised as the same colour and shown in every form with a swatch; `#3366cc80` is shown with 50 % alpha in every form; `rebeccapurple` is recognised.
- [x] Selected text on a page can be sent to Base64, URL, Unix time and HTML entities from the right-click menu, also with dev tools off (they turn on).
- [x] Closing and reopening the dock keeps the utilities' options but not their inputs; no request leaves the browser while using them (checked with a network log).
- [x] Tab environment, time zone `Asia/Tokyo`: `new Date().getTimezoneOffset()` is `-540` and `Intl.DateTimeFormat().resolvedOptions().timeZone` is `Asia/Tokyo` in the tab; another tab of the same window still shows the real time zone.
- [x] Tab environment, language `de-DE, de`: the server receives `Accept-Language: de-DE,de;q=0.9`, `navigator.languages` is `["de-DE", "de"]`, and a site that picks its language from the browser shows German after a reload.
- [x] Tab environment, location Tokyo: after the site is allowed the location, `getCurrentPosition` returns Tokyo's coordinates; "Location unavailable" makes it fail with `POSITION_UNAVAILABLE`.
- [x] Tab environment, user agent and colour scheme: `navigator.userAgent` and the `User-Agent` header show the chosen preset; `matchMedia('(prefers-color-scheme: dark)').matches` follows the choice.
- [x] Overrides survive a reload and a navigation in the tab; turning the tab's dev tools off ends them all (after a reload the page sees the real values); while any is on, the dock shows the bar listing them, with Reset.
- [ ] Works the same on macOS and Windows. (Windows: built and unit-tested by CI, not yet tested by hand.)

## Out of scope

- JWT decoding: the Tokens tool (006) already does it; Utilities links to it.
- Encryption and decryption (AES, RSA), certificate parsing, QR codes — later, if needed.
- A text diff — a candidate for a later version.
- MD5 (decided 2026-11-04: not included; the browser's built-in crypto has no MD5).
- Random test data and Lorem ipsum: a separate feature with many possibilities (028).
- Saving or sharing inputs and results.
- Screen size, touch and device emulation, and network throttling: Chromium DevTools' device mode and Network panel already do them.

## Configuration

None beyond the options each utility remembers (stored with the dev tools' own preferences, per user, not per profile).

## Open questions

None.

## Notes

- 2026-11-04: requested by the user (Base64, Unix time, URL/HTML encoding and similar tools for web app testers, as a new tab in dev tools). Chrome has nothing comparable built in; the closest are online converter sites and DevTools' console (`atob`, `encodeURIComponent`), which this replaces with a UI.
- Implementation hint: all utilities are plain TypeScript in the tools page (`web/tools/`), with unit-tested logic (Node's test runner); hashes use the Web Crypto API (`crypto.subtle`); no new libraries.
- 2026-11-04 (decided with the user, spec set to `ready`): the utilities can be opened from the menu and the command palette; no MD5; "Decode with…" is in the right-click menu always; the first version adds a regular expression tester, a cron explainer and a colour converter; random test data and Lorem ipsum become their own feature (028); the order of the list stays as written.
- 2026-11-04 (user's request): Tab environment added — change the current tab's time zone, language, location and similar while its dev tools are on, to test how a site behaves elsewhere. Chosen while writing: user agent and colour scheme / reduced motion included as "and similar"; location presets as in Chrome's DevTools Sensors panel plus Prague; overrides not inherited by tabs opened from the tab (as in Chrome's DevTools, which emulates per tab). Chromium's own DevTools has some of these spread over the Sensors, Rendering and Network conditions panels; Tab environment gathers them, adds `Accept-Language`, and shows that they are on.
- Implementation hint (to verify against CEF 154 and the protocol): Chrome DevTools Protocol on the tab's own session — `Emulation.setTimezoneOverride`, `Emulation.setLocaleOverride`, `Emulation.setGeolocationOverride`, `Network.setUserAgentOverride` (with `acceptLanguage` and `userAgentMetadata`), `Emulation.setEmulatedMedia`; these methods must be added to the tools page's allowed list (`src/core/devtools_tools.cc`). Overrides are set again after a renderer crash or a cross-process navigation if the protocol does not keep them. Check whether workers and service workers of the page see the overrides; record differences in these notes.
- 2026-11-04 (user's decision): the utilities stay only in the dev tools dock, as a tab next to Site data and Requests, with no separate window; the menu, the command palette and "Decode with…" turn on the tab's dev tools when they are off.
- Choices made while writing (Chrome has nothing comparable): cron accepts 5 fields and 6 fields with seconds first (Spring/Quartz style); regular expressions use JavaScript syntax.
- 2026-11-04 (implementation): verified against CEF 156's protocol. The overrides are sent on the tab's own DevTools session (`DevToolsChannel::SetEnvironment`) and are kept by Chromium across reloads and navigations, including cross-process ones; turning the dev tools off sends the clearing commands before the session closes. Methods used: `Emulation.setTimezoneOverride`, `Emulation.setLocaleOverride`, `Emulation.setUserAgentOverride` (it also sets `Accept-Language`), `Emulation.setGeolocationOverride` / `clearGeolocationOverride` (an override without coordinates is "position unavailable"), `Emulation.setEmulatedMedia`. They are sent by the browser side, not by the tools page, so the page's allowed list is unchanged; the page asks through `tools.setEnvironment`, which the C++ side validates (time zone, language tags, coordinates, user agent length).
- `acceptLanguage` is sent as a plain comma list (`de-DE,de`): Chromium adds the `q` values itself (`de-DE,de;q=0.9`) and uses the list for `navigator.languages`; sending `q` values put them into `navigator.languages`.
- Language alone keeps the real user agent and its client hints (`userAgent` sent empty). A user agent preset uses the browser's own Chrome version for the Chrome and Edge presets; Safari and Firefox presets carry fixed versions (Safari 18, Firefox 140), updated with the presets.
- Location presets: the eight of Chrome's Sensors panel plus Prague (`kLocationPresets` in `web/tools/environment_logic.ts`).
- An invalid time zone is rejected by the page (`Intl`), as the protocol accepts unknown names silently.
- Regular expressions run in a worker that is stopped after 2 seconds, so a runaway pattern (catastrophic backtracking) cannot freeze the dock.
- Cron: `L`, `W`, `#` and `H` (Quartz/Jenkins extensions) are reported as not supported rather than as invalid.
- 2026-11-04 (user's decision, criterion reworded with approval): Brouk's own internal pages open Chromium's DevTools in a window of their own (006), without our dock, so Utilities are not offered there; the menu, palette and "Decode with" do nothing on an internal page. The converters work on any web page and on an empty tab.
- HTML entities decode numeric entities and the named ones of HTML 4 plus `&apos;` (a table in `web/tools/util_html.ts`). HTML5's ~2,000 rarer names would need parsing HTML in the privileged tools page (blocked by the project's opengrep rule) or a large table; unknown names stay as they are.
- 2026-11-05: tested by the user on macOS; spec set to `done`. Windows is built and unit-tested by CI but not yet tested by hand.
