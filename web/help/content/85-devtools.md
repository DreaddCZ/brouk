## Developer tools {#devtools}

Developer tools are switched on **per tab** with the hammer button next to the address bar (<kbd>⌥⌘I</kbd>), or with <kbd>⌥⌘J</kbd> (console), <kbd>⌥⌘C</kbd> (pick an element) and right-click → **Inspect**.

- A tab opened from a tab with developer tools on starts with them on; other tabs start off.
- Turning them on opens the **dock** and starts recording the tab's requests; [request rules](#devtools-request-rules) apply only to such tabs. Turning them off (or closing the dock) stops both.
- The dock sits at the bottom, left or right, or in its own window — the buttons in its header. Its size and position are remembered.
- The dock's tabs: **DevTools** (Chromium's own: Elements, Console, Sources, Network…) and Brouk's tools below. They work only with the tab's own profile.
- Brouk's own pages (Settings, History…) can be inspected the same way.

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

### JSON viewer {#devtools-json-viewer}

JSON documents open as a collapsible tree in every tab — developer tools on or off: **Tree / Raw / Pretty**, search over keys and values, **Copy value** and **Copy path** (`$.items[0].name`), and clickable addresses. Large documents stay fast. Turn it off in Settings → Developer tools.

### Remote debugging {#devtools-remote-debugging}

Tools like Playwright and Puppeteer can control Brouk over the Chrome DevTools Protocol when **remote debugging** is on (Settings → Developer tools, then restart; or start Brouk with `--remote-debugging-port=9222`). It listens on this Mac only. While it is on, every window shows a warning: **any program on your Mac can then control tabs of all profiles, including their sign-ins.** Keep it off when you don't need it.
