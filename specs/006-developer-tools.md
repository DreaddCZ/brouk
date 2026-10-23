# 006 – Developer tools

- **Status:** in-progress
- **Priority:** P1
- **Depends on:** 001, 003, 004, 005, 010

## Goal

Give web developers the full Chromium DevTools they know, plus our own tools for the work this browser is built for: debugging sites and APIs while signed in to many accounts in separate profiles — inspecting and clearing a site's data per profile, reading auth tokens, viewing JSON, changing requests on the fly, and taking API calls over to Postman or Bruno.

## Description

### Dev tools dock

- Developer tools open in a **dock** attached to a tab. The dock has tabs: **Chromium DevTools** (the stock frontend included in CEF) and each of our own tools (below).
- The dock can sit at the **bottom, right or left** of the page, or be **undocked into a separate window**, as in Chrome. It can be resized. The last position and size are remembered and used the next time the dock opens.
- The dock belongs to its browser tab: each tab has its own dock state (selected tool), and switching tabs shows that tab's dock, or none. In a window with several profiles, a tab's dock always works with that tab's profile.
- Opening: see "Dev tools toggle" below.
- Chromium DevTools provides: Elements, Console, Sources/debugger, Network, Performance, Memory, Application/storage, device/responsive emulation, and further panels such as Lighthouse or Recorder where the CEF build includes them.
- Our internal HTML pages (settings, history, password manager, etc.) can be inspected the same way, to help develop the browser itself.

### Dev tools toggle (per tab)

- Dev tools are switched **on or off per tab** — not per profile group or profile. A new tab starts with them **off**, except a tab opened from another tab (a link, a popup such as a login window, or "open in new tab"): it **inherits** the opener tab's state.
- A **toggle in the toolbar, next to the address bar** (on its right by default, or on its left — a setting), is **always displayed** and shows the current tab's state. Switching tabs updates it.
- Turning the toggle **on** for a tab opens its dock and starts recording the tab's requests; request rules start to apply to that tab. Turning it **off** closes the dock, stops recording and stops request rules for that tab. Closing the dock (or its undocked window) turns the toggle off.
- The state stays with the tab across navigations until the user switches it off or closes the tab.
- Other ways to turn it on for the current tab: shortcuts `Cmd+Opt+I` (on/off), `Cmd+Opt+J` (console), `Cmd+Opt+C` (pick an element) on macOS; `F12`, `Ctrl+Shift+I/J/C` on Windows — all remappable (003); "Inspect" in the page's context menu, which also opens the Elements panel on the clicked element; the command palette (003).

### Our own tools (tabs in the dock)

Built as HTML/TypeScript pages (001) on top of the Chrome DevTools Protocol and CEF's request handling. They only read or change data of the tab's own profile.

1. **Site data** — for the current site in the current profile: cookies (name, value, domain, path, expiry, flags), local and session storage, IndexedDB databases, cache storage and service workers. Cookies and storage entries can be viewed, edited, added and deleted; IndexedDB databases, cache and service workers can be deleted. **Clear site data** removes everything for the site in one action, after confirmation, without touching other sites or other profiles.
2. **Tokens** — finds JWTs in the tab's request and response headers (e.g. `Authorization: Bearer`), cookies and storage, and accepts pasted tokens. Shows the decoded header and payload, expiry and issue times as local time and relative ("expires in 12 min", "expired 3 h ago"), and common claims such as issuer, audience, tenant, subject and scopes. Signatures are not verified, and this is shown. Tokens that are not JWTs are listed as not decodable. Tokens never leave the browser.
3. **Requests** — a list of the tab's network requests with filters (type, method, status, text), defaulting to fetch/XHR. Requests can be selected and **exported** (see below). It also feeds the Tokens tool.
4. **Request rules** — per-profile rules that change requests on the fly. A rule has a URL pattern (same syntax as link routing in 010: host, subdomain wildcard, path prefix) and one action: add/replace/remove a request header, add/replace/remove a response header, redirect to another URL, or block. Rules apply only to tabs of that profile that have dev tools switched on, so normal browsing is never changed. Rules can be switched on and off one by one, and all rules of a profile can be paused at once. While any rule affected the current tab's page, the dev tools toggle shows an indicator, so changed behaviour is never invisible.
5. **JSON viewer** — when a tab opens a JSON document (JSON content type, or a body that parses as JSON), it is shown as a formatted, collapsible tree with search, a raw/pretty switch, and copy of a value or its path. Large documents stay responsive. This works in every tab, whether its dev tools are on or off, and can be turned off in settings. The Requests tool uses the same viewer for response bodies.

### Export API calls to Postman / Bruno

- One or more selected requests from the Requests tool are exported as a **Postman Collection (format v2.1) JSON file**, which both Postman and Bruno can import.
- Each request keeps its method, URL with query parameters, headers and body (JSON, form or raw). Request names are generated from method and path; requests are grouped in folders by host.
- **Secrets become variables:** `Authorization` and `Cookie` headers and other secret-looking values (a configurable list of header names, e.g. `x-api-key`) are replaced by `{{variables}}`. The real values go into a separate **environment file**; when exporting, the user chooses whether to create it. Without it, the collection contains no secrets.
- Each host is replaced by a `{{baseUrl…}}` variable defined in the environment file, so the collection can be pointed at another environment.

### Remote debugging

- Remote debugging (Chrome DevTools Protocol over a port, used by Playwright, Puppeteer and Chrome's `chrome://inspect`) is **off by default**.
- It can be switched on by a global setting or by the `--remote-debugging-port` command-line flag. It applies to the whole app and needs a restart (004).
- It listens on **localhost only**.
- While it is on, every window shows a clear indicator in the toolbar, with the port and a warning that tabs of **all profiles**, including their logged-in sessions, can be controlled through it.

## Acceptance criteria

Dock and DevTools
- [ ] The dock can be placed at the bottom, right or left, or undocked into a separate window, and resized; the last position and size are used the next time it opens.
- [ ] Each tab keeps its own dock state; switching tabs shows the newly selected tab's dock, or none if its dev tools are off.
- [ ] In a window with tabs of several profiles, each tab's dock and tools work only with that tab's profile.
- [ ] Elements, Console, Sources, Network, Performance, Memory and Application panels work.
- [ ] Device/responsive emulation works.
- [ ] Internal HTML pages of the browser can be inspected.
- [ ] Dock shortcuts can be remapped.

Dev tools toggle
- [ ] The toggle is always displayed next to the address bar, on its right by default; a setting moves it to the left.
- [ ] A new tab not opened from another tab has dev tools off; a tab opened from another tab (link, popup, "open in new tab") has the same dev tools state as its opener.
- [ ] Switching the toggle on or off affects only the current tab; other tabs, also of the same profile and window, keep their state.
- [ ] The toggle shows the state of the current tab and updates when switching tabs.
- [ ] Turning it on opens the dock; turning it off, or closing the dock or its undocked window, closes the dock and turns the toggle off.
- [ ] The shortcuts, the command palette and right-click "Inspect" turn dev tools on for the current tab; "Inspect" selects the clicked element in Elements.
- [ ] Dev tools stay on in a tab across navigations until switched off.

Site data
- [ ] Cookies and local/session storage of the current site can be viewed, edited, added and deleted; IndexedDB databases, cache storage and service workers can be deleted.
- [ ] "Clear site data", after confirmation, removes all data of the current site in the current profile only; other sites and the same site in other profiles keep their data.

Tokens
- [ ] JWTs from request/response headers, cookies and storage of the current tab are listed and decoded; a pasted JWT is decoded too.
- [ ] Expiry and issue times are shown as local time and relative time; expired tokens are marked.
- [ ] The view states that signatures are not verified; non-JWT tokens are shown as not decodable.

Requests and export
- [ ] The Requests tool lists the tab's requests and can filter them by type, method, status and text.
- [ ] Selected requests export to a Postman Collection v2.1 JSON file that imports without errors into current Postman and Bruno, keeping method, URL, query parameters, headers and body.
- [ ] In the exported collection, `Authorization`, `Cookie` and configured secret headers are `{{variables}}`; the collection file contains no secret values.
- [ ] The user can choose to also export an environment file with the secret values and base URLs.

Request rules
- [ ] A rule can add, replace or remove a request header, add, replace or remove a response header, redirect, or block, for URLs matching its pattern (010 syntax).
- [ ] Rules belong to a profile, are stored in its settings (004), and never apply to tabs of other profiles.
- [ ] Rules apply only to tabs with dev tools on; with the same rule, a tab with dev tools off loads the page unchanged.
- [ ] Rules can be switched on and off individually, and all rules of a profile can be paused at once.
- [ ] When a rule affected the current tab, the dev tools toggle shows an indicator.

JSON viewer
- [ ] A tab opening a JSON document shows it as a collapsible tree with search, raw/pretty switch, and copy of value and path.
- [ ] A JSON document of 10 MB opens without freezing the window.
- [ ] The JSON viewer can be turned off in settings.

Remote debugging
- [ ] Remote debugging is off by default and can be enabled by a setting (after restart) or by the command-line flag.
- [ ] When enabled, it accepts connections only from localhost.
- [ ] While it is enabled, every window shows an indicator with the port and the all-profiles warning.

## Out of scope

- Adding panels inside the stock Chromium DevTools frontend; our tools live as separate tabs in the dock.
- Rewriting the Chromium DevTools frontend.
- Verifying token signatures.
- Exporting native Bruno collection folders (Postman format covers both for now).
- Restricting remote debugging to some profiles.
- Moving cookies or sessions between profiles (would break profile isolation, 005).

## Configuration

Per profile:
- Request rules (none).

Global:
- Toggle position: right or left of the address bar (right).
- Dock position (bottom) and size; remembered automatically.
- JSON viewer (on).
- Secret header names for export (`Authorization`, `Cookie`, `x-api-key`, `api-key`, `x-auth-token`).
- Remote debugging (off), port (9222) — restart required.
- Shortcuts for opening the dock and individual tools (see 003).

## Open questions

None.

## Notes

- 2026-10-21 (decision in 001): the user wants developer tools that differ from Chrome's. Phase 1 ships the stock Chromium DevTools frontend included in CEF; our own tools are built on the Chrome DevTools Protocol and replace or extend panels step by step.
- 2026-10-21 (developer tools review): own tools agreed — site data panel, token decoder, JSON viewer, request rules, and export of API calls for Postman/Bruno. Toolbar toggle next to the address bar; Chrome-like docking; our tools as tabs in the same dock; secrets in exports become variables; remote debugging off by default, localhost only, with an indicator.
- 2026-10-21 (developer tools review): developer mode per profile replaced by a **per-tab** dev tools toggle — off by default, always displayed left or right of the address bar. Request rules apply only to tabs with dev tools on.
- 2026-10-21 (developer tools review): a tab opened from another tab inherits its dev tools state; other new tabs start off. Spec set to `ready`.
- 2026-10-21 (readiness review): the dependency on 010 is only its **URL pattern syntax** (host, subdomain wildcard, path prefix; most specific wins), which is agreed even though 010 is still `draft` (its open questions concern the picker dialog). Implement the pattern matcher as a shared core module in `src/core/` so 006 and 010 use the same code.
- 2026-10-21 (readiness review, provisional — revisit during testing): a tab opened from another tab inherits the opener's dev tools state **also when link routing (010) opens it in a different profile**. Request rules still apply only per profile (the new tab gets its own profile's rules, never the opener's).
- CEF (checked against `include/cef_browser.h` and `cef_life_span_handler.h`, master 2026-10-20): `CefBrowserHost::ShowDevTools` opens DevTools in its own browser; with a client-provided parent view we place it ourselves, so docking, resizing and undocking are our native layout code. `OnBeforeDevToolsPopup` applies only to Chrome style.
- CEF (`include/internal/cef_types.h`): remote debugging is `CefSettings.remote_debugging_port` (app-wide, set at startup) or the `remote-debugging-port` switch; hence global and restart-required. When a cache directory is set, the port is written to `<cache-dir>/DevToolsActivePort`.
- The stock DevTools frontend cannot get our own context-menu items (e.g. "export" in its Network panel); that is why the Requests tool exists. Request capture uses the CDP Network domain and starts when the tab's dev tools are switched on (requests before that are not listed, as in Chrome).
- Request rules: header changes and redirects/blocking can likely be done in CEF's resource request handling (`CefResourceRequestHandler`), without the CDP Fetch domain; check the headers before choosing.
- Verify during implementation: which extra panels (Lighthouse, Recorder) work in CEF's bundled frontend; whether Bruno imports a Postman environment file alongside the collection (if not, document how to set the variables in Bruno).
- 2026-10-23 (decided with the user, implementation): CEF 154 cannot show the stock DevTools inside our native window on macOS (`ShowDevTools` with a parent view crashes: "Alloy style is not supported for this browser"; DevTools is always a Chrome-style window). Brouk therefore embeds the DevTools frontend shipped in CEF (`devtools://devtools/bundled/devtools_app.html`) in a normal browser view in the dock and acts as its embedder itself, like Chrome's DevToolsUIBindings and Content Shell: a `DevToolsHost` object in the frontend page, embedder messages answered in `src/cef/devtools_frontend_client.cc`, and Chrome DevTools Protocol messages passed to the tab with `SendDevToolsMessage` and a message observer. The frontend's own settings are kept in `devtools-preferences.json`. Because CEF's protocol session stays attached for the tab's lifetime, closing the dock undoes what DevTools changed (debugger pauses, emulation, throttling, highlights). Not yet supported in the embedded frontend: loading source maps from the network, saving files from Sources, workspaces (file system folders), and Google's online DevTools features (AI assistance). Risk: the embedder interface can change with Chromium updates; if DevTools stops working after a CEF update, this is the place to look.
- 2026-10-23 (implementation): the toggle is a hammer button (filled and in the accent colour when on). The dock header has buttons for bottom, left, right and a separate window, and a close button; the position and size are shared by all tabs and remembered as settings (`devtools.dock_position`, `devtools.dock_size`). Remote debugging setting and port are on the settings page (restart required); `--remote-debugging-address` is forced to 127.0.0.1. Shortcut remapping waits for 003's shortcut settings.
- 2026-10-23 (implementation): JSON viewer. Chromium shows JSON (and plain text) as a `<pre>`; for documents with a JSON content type (`application/json`, `text/json`, `*+json`) or plain text that starts with `{` or `[` and parses, a script added to the page's main frame replaces it with the tree (Tree / Raw / Pretty, search over keys and values with next/previous, "Copy value" / "Copy path" as `$.a[0]["b c"]`, http(s) strings as links). It is TypeScript (`web/jsonview/viewer.ts`, unit-tested) compiled at build time and embedded in the renderer; it runs wrapped in a function, so pages see none of its names, and it sets page data only as text. Large documents: nodes are built when expanded, members in chunks of 200, and a search match deep in a long list shows a window of rows around it; a generated 10 MB document opened in under 4 s and searching its last item took under 3 s. Setting: Settings > Pages > "Show JSON documents as a tree" (`content.json_viewer`), applied live (reload the page). The Requests tool will reuse the viewer.
- 2026-10-23 (implementation): our own tools are one internal page, `brouk://tools/#<tool>`, shown in the dock when a tool tab is chosen (the dock's header has "DevTools | Site data", Tokens and Requests follow). The page reaches the dock's tab through Chrome DevTools Protocol commands on its own channel (`src/cef/devtools_channel.cc`), limited to the methods in `src/core/devtools_tools.cc`; it can neither run code in the page nor reach other tabs, so it only sees the tab's profile. Site data: cookies (view, edit, add, delete), local and session storage (view, edit, add, delete), IndexedDB databases and cache storage (list, delete), service workers (their storage use; delete), and "Clear site data" with an inline confirmation: Chromium's clear for the origin, plus the page's cookies set for a parent domain and session storage. Service workers are not listed one by one (listing needs protocol events, which the tools do not receive yet).
- 2026-10-23 (implementation): Requests. Recording starts when a tab's dev tools are switched on (Network events of the tab's protocol session; `src/core/request_log.cc`, the last 2000 requests, request bodies up to 256 kB, headers as actually sent including cookies). The list filters by type (Fetch/XHR by default), method, status (2xx–5xx, failed) and text; details show headers and the request body, and load the response body on request (JSON pretty-printed; the JSON viewer's tree is not used there yet). Export: selected requests become a Postman Collection v2.1 (folders by host, names "METHOD /path", query parameters, JSON/form/raw bodies); each host is a `{{baseUrl_<host>}}` variable, also defined in the collection so it works on its own; values of secret headers (setting `devtools.secret_headers`, default Authorization, Cookie, x-api-key, api-key, x-auth-token) become `{{variables}}` and go only into the optional `.postman_environment.json` saved next to it. 2026-10-23: the user confirmed that the export works and imports into Bruno.
- 2026-10-23 (implementation): Tokens. JWTs from the recorded request and response headers (and other `Bearer` tokens, shown as not decodable), the site's cookies and local/session storage, and a paste box. Shows issuer, audience, tenant, subject, name, scopes, roles, issue/valid-from/expiry times as local and relative time, "Expired" for expired tokens, "Signature not verified", and the decoded header and payload. Nothing is sent anywhere.

- 2026-10-23 (implementation): Request rules. Stored per profile in `profiles/<id>/request-rules.yaml` (machine-written like the settings, reloaded live when edited by hand; `paused` and a list of rules with `pattern`, `action` — `set-request-header`, `remove-request-header`, `set-response-header`, `remove-response-header`, `redirect` with `url`, `block` — `header`, `value`, `enabled`). Patterns use the 010 syntax (`src/core/url_pattern.cc`, shared with link routing). Applied through the Chrome DevTools Protocol Fetch domain of the tab's own protocol session, so only tabs with dev tools on are touched; when several rules match, the most specific pattern wins for redirect/block, header rules apply from least to most specific. Block fails the request with "blocked by client"; redirect answers with a 307 to the target. The toggle turns orange (with a tooltip) when a rule changed a request of the current page, and resets on the next page load.
- 2026-10-23 (decision, user): response header rules are **applied by Brouk loading the request itself.** CEF 154 hands the page the response headers it received from the network service (its request proxy, `proxy_url_loader_factory.cc`, replaces the response's headers with those from `OnHeadersReceived`), so changes made with `Fetch.continueResponse` are accepted but lost, and CEF's own API (`CefResourceRequestHandler::OnResourceResponse`) cannot change response headers either. So a request that a response header rule matches is loaded by Brouk (`src/cef/own_request_loader.cc`, a `CefURLRequest` in the tab's profile, with the page's method, headers after the request header rules, referrer and body) and the paused request is answered with `Fetch.fulfillRequest` and the changed headers. Redirects are handed back to the tab, which follows them itself. Differences from the page's own request: the whole response is loaded before the page gets it (no streaming; over 64 MB the request fails); event streams (`Accept: text/event-stream`) and connection upgrades are not loaded this way, so response header rules do not apply to them; neither do bodies the protocol event does not include (e.g. file uploads); the profile's cookies are always sent, also where the page's `fetch` would leave them out; `Sec-Fetch-Site`/`Sec-Fetch-Mode` are set by the network stack as for a request from the target site; no login prompt (HTTP authentication fails).
- 2026-10-23 (implementation): known limits of request rules — header rules see the request before the network stack adds cookies, so cookies sent by the browser cannot be removed or changed by rules; while rules are active, the stock DevTools' own request interception (local overrides) does not work, because the paused requests are answered by Brouk and not shown to the frontend.
- 2026-10-23 (implementation): internal pages. Settings, the profile manager and the dock's tools page have a context menu (editing commands, Copy, Reload, and **Inspect**, as Chrome's pages have), and View > Developer (⌥⌘I/J/C) works in their windows. Their DevTools opens in a window of its own ("Developer Tools – <page>"), only Chromium DevTools without our tools: internal pages have no profile, requests or rules. ⌥⌘I in that window closes it. Links in it open as a tab of the default profile.
- 2026-10-23 (implementation): the Requests tool shows JSON response bodies with the JSON viewer's tree (Tree / Raw / Pretty, search, copy); the tools page loads the viewer as `shared/json_viewer.js`. The viewer's styles are now scoped to its host and added as a constructed style sheet, so it also works on JSON responses whose Content-Security-Policy forbids inline styles. Request bodies stay plain (pretty-printed) text.
- 2026-10-23 (implementation): Site data lists service workers one by one (scope, script, state; Delete unregisters), from the ServiceWorker protocol events of the tab's session, which the dock follows from when it opens (`src/core/service_workers.cc`). If none is known but the site stores service worker data, that data can still be deleted as before.
- 2026-10-23: manual test checklist for the whole spec: `docs/testing/006-developer-tools.md`. The user tests 006 once, when all of it is done, not per pull request.
- 2026-10-23 (003 shortcuts): the dev tools shortcuts are remappable as `shortcuts.devtools`, `shortcuts.devtools_console` and `shortcuts.devtools_inspect` (settings page, Shortcuts section).
