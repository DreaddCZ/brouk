# 027 – Utilities for testers: manual test checklist

Run on macOS and on Windows. Test page: `docs/testing/pages/environment.html` (open it as a file, or serve the folder with `python3 -m http.server`).

## Opening

1. With dev tools off, choose **Utilities** (macOS: View › Developer; Windows: More tools). Expected: dev tools turn on and the dock shows the Utilities tab.
2. Command palette → type "Base64". Expected: an entry Base64 marked Utilities; it opens the dock at Base64. Also "Utilities" alone opens the list.
3. In the list, type "cron" into the filter and press Return. Expected: only Cron expressions is listed and it opens, with the cursor in its input.
4. Select `eGIxc2RjV2xiM1hFaldyRHZTQnJ4YS9GaUE9PQ` on a page, right-click → **Decode With › Base64**, with dev tools off. Expected: dev tools turn on, Base64 opens in Decode with the text as input. Repeat with URL (`a%20b%26c`), Unix time (`1730000000`) and HTML entities (`&lt;b&gt;`).

## Converters

5. Base64: `Žluťoučký kůň` → `xb1sdcWlb3XEjWvDvSBrxa/FiA==`; Swap gives the text back. Decode `xb1sdcWlb3XEjWvDvSBrxa_FiA` (URL-safe) → the same text. Decode `abc$def` → a message with the position. Decode `/w==` → hex `ff`, 1 byte. Drop an image → its Base64 and a `data:image/…` URL.
6. Unix time: `1730000000` and `1730000000000` → 2024-10-27 03:33:20 UTC, with local, ISO 8601 and relative time. Type a date → the timestamp in seconds and ms. **Now** fills the current time. Choose `America/New_York` → the time in that zone.
7. URL encoding: `a b&c=d/é` as URL part → `a%20b%26c%3Dd%2F%C3%A9`; as form data → `a+b%26c%3Dd%2F%C3%A9`; decode back. `%E9` and `%G1` are reported as invalid.
8. URL parser: paste `https://example.com/a?x=1&name=J%C3%A1n#top`. Expected: parameters `x` = `1`, `name` = `Ján`; edit a value, add and remove a parameter → the URL changes.
9. HTML entities: `<a href="x">&</a>` encodes and decodes; `&eacute;`, `&#233;`, `&#xE9;` decode to `é`.
10. Hashes: SHA-256 of `abc` = `ba7816bf…15ad` (compare `printf abc | shasum -a 256`); HMAC-SHA256 with key `key` of `abc` matches `printf abc | openssl dgst -sha256 -hmac key`; drop a file → matches `shasum -a 256 <file>`.
11. UUID: generate 5 v4 and 5 v7, upper case, without hyphens. Paste a v7 → version 7 and its time.
12. JSON: escape `He said "hi"\ and left` with a line break and `é` → a JSON string; Unescape gives it back. Format `{"a":[1,2` → the error position.
13. Number bases: decimal `9007199254740993` → hex `20000000000001`, and back. Text and hex bytes: `é` → `c3 a9`.
14. Regular expressions: pattern `(?<y>\d{4})-(\d{2})`, flags `g`, text `2024-10 and 2025-01` → two highlighted matches with groups `y` and 2; replacement `$2/$<y>` → `10/2024 and 01/2025`. Pattern `(` → the browser's error. Pattern `(a+)+$` on 40 × `a` + `b` → stopped after 2 seconds, the dock stays responsive.
15. Cron: `30 8 * * 1-5` → "At 08:30, on Monday through Friday" and 5 next runs; `0 */15 * * * *` → every 15 minutes; `@daily` works; `61 * * * *` points at the minute field.
16. Colours: `#3366cc`, `rgb(51, 102, 204)`, `hsl(220, 60%, 50%)` → the same colour in every form with a swatch; `#3366cc80` → 50 % alpha in every form; `rebeccapurple` is recognised; the colour picker changes the input.
17. Set options (e.g. URL-safe, a time zone), type inputs, turn dev tools off and on. Expected: options kept, inputs empty.
18. With Chromium's Network panel open, use several converters. Expected: no requests.

## Tab environment

19. Open the test page, Utilities → Tab environment → switch on Time zone `Asia/Tokyo`, then **Reload the tab**. Expected: the page shows offset `-540` and `Asia/Tokyo`; the dock shows the orange bar "This tab uses: time zone Asia/Tokyo"; another tab shows the real zone.
20. Switch on Language `de-DE, de`, reload. Expected: `navigator.languages` is `["de-DE","de"]`, the request header (served page, or a site such as one showing headers) is `Accept-Language: de-DE,de;q=0.9`; a site that chooses its language from the browser shows German.
21. Switch on Location Tokyo; on the test page press "Get location" and allow it. Expected: Tokyo's coordinates. Choose "Location unavailable" → the error `POSITION_UNAVAILABLE` (code 2).
22. Switch on User agent "Chrome on Android" and Colour scheme Dark, reload. Expected: `navigator.userAgent` and the header show Android; the page is dark (`prefers-color-scheme: dark` true). Reduced motion → `prefers-reduced-motion: reduce` true.
23. Navigate the tab to another site and back. Expected: overrides still on, the bar still shown.
24. Press **Reset** in the bar. Expected: the bar goes away; the Tab environment switches are all off; after a reload the page sees the real values.
25. Switch some on again, then turn dev tools off and reload. Expected: real values; turning dev tools on again shows nothing switched on, but the chosen values (e.g. Asia/Tokyo) are still filled in.
26. Type `Mars/Base` as the time zone with the switch on. Expected: "“Mars/Base” is not a time zone."; nothing applied.
