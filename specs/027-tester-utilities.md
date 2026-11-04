# 027 – Utilities for testers in developer tools

- **Status:** ready
- **Priority:** P2
- **Depends on:** 006 (developer tools dock and the tools page)

## Goal

Testers of web apps keep converting small values by hand or in random web sites: decoding a Base64 string from a header, reading a Unix timestamp from an API response, URL-encoding a parameter, unescaping HTML. Pasting tokens, IDs and customer data into third-party sites leaks them. Brouk should have these converters at hand, in the dev tools, working offline, so values never leave the computer.

## Description

A new tab **Utilities** in the dev tools dock (006), next to Site data, Tokens, Requests and Rules. It is part of the tools page (`brouk://tools/#utilities`) and, like the other tools, works for the current tab; unlike them it needs nothing from the tab (no protocol access), so it also works on internal pages and with no page loaded.

The utilities also open **on their own**, without dev tools for any tab: **Utilities** in the menu (macOS: View › Developer; Windows: the app menu's More tools) opens them in a window of their own (like Settings and Help), and the command palette (003) has an entry per utility ("Base64", "Unix time", "URL encoding"…) that opens that window at the utility.

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

### Shared behaviour

- **From the page:** selected text can be sent to a utility from the page's right-click menu (**Decode with › Base64 / URL / Unix time / HTML entities**, shown whenever text is selected); the utility opens with the text as input — in the dock when the tab's dev tools are on, otherwise in the Utilities window.
- Utilities remember their last choice of options (e.g. URL-safe, time zone), **not** their input or output: values may be tokens or personal data and are gone when the dock closes.
- Everything runs inside the tools page; nothing is sent over the network or written to disk.
- Keyboard: every control can be reached with the keyboard; the utility list can be filtered by typing.

## Acceptance criteria

- [ ] The dev tools dock has a **Utilities** tab; it works with any tab, also an internal page or an empty tab.
- [ ] The menu item Utilities opens the utilities in their own window, without dev tools; a command palette entry opens that window at its utility.
- [ ] Each utility in the list converts as the input changes, in both directions where it has two, with Copy and Swap.
- [ ] Base64: `Žluťoučký kůň` encodes to `xb1sdcWlb3XEjWvDvSBrxa/FiA==` and back; URL-safe input decodes; invalid input shows the position of the problem; binary output is shown as hex; a dropped file gives its Base64 and data URL.
- [ ] Unix time: `1730000000` and `1730000000000` both show 2024-10-27 03:33:20 UTC, with local time, ISO 8601 and relative time; a date gives the timestamp back; Now works; another time zone can be chosen.
- [ ] URL encoding: `a b&c=d/é` encodes as a component to `a%20b%26c%3Dd%2F%C3%A9`, as form data with `+` for the space, and decodes back; `%E9` alone or `%G1` is reported as invalid.
- [ ] URL parser: query parameters are listed decoded; editing one or adding/removing one updates the URL.
- [ ] HTML entities: `<a href="x">&</a>` encodes and decodes; `&eacute;`, `&#233;` and `&#xE9;` decode to `é`.
- [ ] Hashes and HMAC give the same results as `shasum` / `openssl` for text and for a file.
- [ ] UUIDs generated as v4 and v7 are valid; a pasted v7 UUID shows its time.
- [ ] JSON escaping round-trips text with quotes, backslashes, newlines and non-ASCII characters; invalid JSON shows the error position.
- [ ] Number bases convert between decimal, hex, octal and binary, also for numbers above 2^53.
- [ ] Regular expressions: `(?<y>\d{4})-(\d{2})` with `g` on `2024-10 and 2025-01` highlights two matches and lists both groups (`y` named); a replacement `$2/$<y>` gives `10/2024 and 01/2025`; an invalid pattern shows the browser's error.
- [ ] Cron: `30 8 * * 1-5` is explained as at 08:30, Monday to Friday, with the next 5 run times; `0 */15 * * * *` (6 fields) runs every 15 minutes; `@daily` works; `61 * * * *` points out the minute field.
- [ ] Colours: `#3366cc`, `rgb(51, 102, 204)` and `hsl(220, 60%, 50%)` are recognised as the same colour and shown in every form with a swatch; `#3366cc80` is shown with 50 % alpha in every form; `rebeccapurple` is recognised.
- [ ] Selected text on a page can be sent to Base64, URL, Unix time and HTML entities from the right-click menu, also with dev tools off (then the Utilities window opens).
- [ ] Closing and reopening the dock keeps the utilities' options but not their inputs; no request leaves the browser while using them (checked with a network log).
- [ ] Works the same on macOS and Windows.

## Out of scope

- JWT decoding: the Tokens tool (006) already does it; Utilities links to it.
- Encryption and decryption (AES, RSA), certificate parsing, QR codes — later, if needed.
- A text diff — a candidate for a later version.
- MD5 (decided 2026-11-04: not included; the browser's built-in crypto has no MD5).
- Random test data and Lorem ipsum: a separate feature with many possibilities (028).
- Saving or sharing inputs and results.

## Configuration

None beyond the options each utility remembers (stored with the dev tools' own preferences, per user, not per profile).

## Open questions

None.

## Notes

- 2026-11-04: requested by the user (Base64, Unix time, URL/HTML encoding and similar tools for web app testers, as a new tab in dev tools). Chrome has nothing comparable built in; the closest are online converter sites and DevTools' console (`atob`, `encodeURIComponent`), which this replaces with a UI.
- Implementation hint: all utilities are plain TypeScript in the tools page (`web/tools/`), with unit-tested logic (Node's test runner); hashes use the Web Crypto API (`crypto.subtle`); no new libraries.
- 2026-11-04 (decided with the user, spec set to `ready`): the utilities also open without dev tools, in a window of their own from the menu and the command palette; no MD5; "Decode with…" is in the right-click menu always (with dev tools off it opens the Utilities window); the first version adds a regular expression tester, a cron explainer and a colour converter; random test data and Lorem ipsum become their own feature (028); the order of the list stays as written.
- Choices made while writing (Chrome has nothing comparable): the Utilities window is a separate window like Settings and Help, not a tab; cron accepts 5 fields and 6 fields with seconds first (Spring/Quartz style); regular expressions use JavaScript syntax.
