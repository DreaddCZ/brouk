## Developer tools {#devtools}

Developer tools are switched on **per tab** with the hammer button next to the address bar (<kbd>⌥⌘I</kbd>), or with <kbd>⌥⌘J</kbd> (console), <kbd>⌥⌘C</kbd> (pick an element) and right-click → **Inspect**.

- A tab opened from a tab with developer tools on starts with them on; other tabs start off.
- Turning them on opens the **dock** and starts recording the tab's requests; [request rules](#devtools-request-rules) apply only to such tabs. Turning them off (or closing the dock) stops both.
- The dock sits at the bottom, left or right, or in its own window — the buttons in its header. Its size and position are remembered.
- The dock's tabs: **DevTools** (Chromium's own: Elements, Console, Sources, Network…) and Brouk's tools below, among them [Utilities](#devtools-utilities) and the [Security check](#devtools-security-check). They work only with the tab's own profile.
- Brouk's own pages (Settings, History…) can be inspected the same way.

**On Windows:** <kbd>F12</kbd> or <kbd>Ctrl+Shift+I</kbd> (also "⋮" → Developer tools, and the toolbar's button) switches the tab's developer tools on and off. <kbd>Ctrl+Shift+J</kbd> opens the console and <kbd>Ctrl+Shift+C</kbd> picks an element, as in Chrome. Drag the dock's edge to resize it. "Inspect" on one of Brouk's own pages (settings, help, the profile manager) opens DevTools in a window of its own.

### Site data {#devtools-site-data}

Everything the current site stores in this profile: cookies, local and session storage, IndexedDB, cache storage and service workers. View, edit, add and delete cookies and storage entries; delete databases, caches and service workers. **Clear site data** removes all of it for this site only, after you confirm.

### Tokens {#devtools-tokens}

Finds JWTs in the tab's request and response headers, cookies and storage — or paste one. Shows the decoded header and payload, issuer, audience, tenant, subject, scopes and roles, and the times as local time and "expires in 12 min" / "expired 3 h ago". **The signature is not verified.** Tokens never leave Brouk.

### Requests {#devtools-requests}

The tab's requests since developer tools were switched on (the last 2,000), filtered by type (Fetch/XHR by default), method, status and text. Select a request for its headers, request body and response (JSON as a tree).

### Export to Postman or Bruno {#devtools-api-export}

Select requests in **Requests** and export them as a **Postman collection** (v2.1), which Postman and Bruno import:

- each request keeps its method, address, query, headers and body; requests are grouped by host;
- each host becomes a `{{baseUrl_…}}` variable, so the collection can be pointed at another environment;
- secret headers (Authorization, Cookie, API keys — the list is in Settings → [Developer tools](#settings-developer-tools)) become `{{variables}}`. Their real values go into a separate **environment file** only if you choose to create it; without it, the collection contains no secrets.

### Request rules {#devtools-request-rules}

Rules change requests on the fly, per profile, and only in tabs with developer tools on — normal browsing is never changed. Each rule has a [URL pattern](#url-patterns) and one action:

- set or remove a **request header**,
- set or remove a **response header**,
- **redirect** to another address,
- **block**.

Rules can be switched off one by one, or all paused. When a rule changed something in the tab, the hammer button turns orange. When several rules match, the most specific pattern wins for redirect and block; header rules all apply, from the least to the most specific.

Limits: cookies the browser adds can't be changed by rules, and while rules are active, DevTools' own local overrides don't work. The rules are kept in `profiles/<id>/request-rules.yaml` and can be edited there too.

### Utilities {#devtools-utilities}

Small converters for testing, in the **Utilities** tab of the dock. They work offline: what you paste never leaves Brouk, and inputs are forgotten when the dock closes (the options you chose are remembered).

- **Base64** (also URL-safe, and files as Base64 or a `data:` URL), **URL encoding** (`%20`, as a URL part, a whole URL or form data), **URL parser** (query parameters in an editable table), **HTML entities**, **JSON and escaping** (text as a JSON string, format, minify), **Text and hex bytes**, **Number bases**.
- **Unix time:** a timestamp in seconds, milliseconds or microseconds (detected from its size) as local time, UTC, ISO 8601 and relative time — or a date back to a timestamp; any time zone.
- **Hashes and HMAC** (SHA-1, SHA-256, SHA-384, SHA-512) of text or a file; **UUID** v4 and v7, and what a pasted UUID is.
- **Regular expressions** (JavaScript syntax), **Cron expressions** (in words, with the next runs), **Colours** (hex, rgb, hsl, names).

Type in the list's **Filter** box to find one. Select text on a page and right-click **Decode with** to open it in Base64, URL encoding, Unix time or HTML entities; the menu item **Utilities** and the command palette open them too (both turn on dev tools for the tab).

### Tab environment {#devtools-tab-environment}

**Utilities → Tab environment** makes the current tab see another **time zone**, **language** (the `Accept-Language` header, `navigator.languages` and date formatting), **location**, **user agent** or **colour scheme** and reduced motion — to test how a site behaves elsewhere without changing your computer's settings. Switch each one on separately. While any is on, the dock shows which, with **Reset**. Only this tab is affected, only while its dev tools are on; switching them off ends everything. A page that already read a value sees the change after a reload. A site still needs its location permission.

### Security check {#devtools-security-check}

**Security check** looks for common mistakes in the current page's security set-up — the kind OWASP ZAP's passive scan finds — without sending anything: it reads only what the tab already loaded.

- **Cookies:** missing `Secure`, `HttpOnly` on session cookies, `SameSite`; session cookies shared with every subdomain or living longer than 30 days; broken `__Host-` / `__Secure-` prefixes.
- **Headers** of the page and of the site's own API calls: `Content-Security-Policy` (missing or weak), `Strict-Transport-Security`, protection against framing (clickjacking), `X-Content-Type-Options`, `Referrer-Policy`, `Cross-Origin-Opener-Policy`, `Permissions-Policy`, server versions, CORS with credentials, and caching of signed-in responses.
- **The page:** plain HTTP, mixed content, forms that send to HTTP, tokens or passwords in URLs, scripts and styles from other sites without `integrity`, and JWTs kept in local or session storage.

Findings are grouped as **High**, **Medium**, **Low** and **Informational**, rated as OWASP ZAP rates the same issue. Open one to see what was found, why it matters, how to fix it and a link to OWASP's guidance. **Copy as Markdown** copies them for a bug report, with tokens shortened. **Hide for this site** hides a known, accepted finding for the site in this profile; **Show hidden** brings it back. Pages on `localhost` are checked as if they were served over HTTPS.

If the dev tools were turned on after the page loaded, the page's own headers are not known: use **Reload and check**. Only the page's own site is checked, not other sites it loads from. Settings → Developer tools sets which cookie names count as session cookies and which URL parameters count as secrets. A clean result is not a security test: it covers only what a passive check can see.

### JSON viewer {#devtools-json-viewer}

JSON documents open as a collapsible tree in every tab — developer tools on or off: **Tree / Raw / Pretty**, search over keys and values, **Copy value** and **Copy path** (`$.items[0].name`), and clickable addresses. Large documents stay fast. Turn it off in Settings → Developer tools.

### Remote debugging {#devtools-remote-debugging}

Tools like Playwright and Puppeteer can control Brouk over the Chrome DevTools Protocol when **remote debugging** is on (Settings → Developer tools, then restart; or start Brouk with `--remote-debugging-port=9222`). It listens on this Mac only. While it is on, every window shows a warning: **any program on your Mac can then control tabs of all profiles, including their sign-ins.** Keep it off when you don't need it.
