# 029 – Security check in developer tools

- **Status:** ready
- **Priority:** P2
- **Depends on:** 006 (developer tools dock, Requests, Site data)

## Goal

Testers and developers often ship pages with missing security headers or carelessly set cookies: a session cookie readable by scripts, no `Content-Security-Policy`, no `Strict-Transport-Security`, a server announcing its version. Tools such as OWASP ZAP find these with a passive scan, but they need a proxy, set-up and a long report. Brouk should show the basic problems of the current page at a glance, in the dev tools, while testing it anyway — a minimal security check, not a penetration test.

## Description

A new tab **Security check** in the dev tools dock (006), next to Site data, Tokens, Requests, Rules and Utilities (027). (Chromium's DevTools has its own *Security* panel for the connection and certificate; this tab is named differently to avoid confusion.)

The check is **passive**: it only looks at what the tab already loaded — the responses recorded by Requests (006), the site's cookies and the page — and never sends a request or an attack of its own. If the tab's dev tools were turned on after the page loaded, the tab offers **Reload and check**.

It checks the page's main document, the site's cookies, and the Fetch/XHR responses from the page's own site (the same site as the page, subdomains included); responses from other sites (CDNs, analytics) are not checked.

Findings are listed by severity (**High**, **Medium**, **Low**, **Info**), each with a one-line title, what was found (the header or cookie, its value), why it matters, how to fix it, and a link to the matching OWASP cheat sheet. A summary at the top counts them ("2 high, 3 medium, 5 passed"); passed checks can be shown too, so the tester sees what was checked. **Check again** re-runs it after the next requests. Nothing is shown outside the tab (no badge on the dock).

- **Copy as Markdown** copies the findings (with the page's URL and the time of the check) for a bug report. Cookie values and secrets found in URLs are shortened (`eyJhbGci…`) so a report never carries a working token.
- A finding can be **hidden** for the site ("known, accepted"): remembered per profile and site, counted as "3 hidden" and shown again with **Show hidden**, where each can be unhidden.

### Checks (first version)

**Cookies** (the site's cookies, as in Site data):

- No `Secure` on a site served over HTTPS.
- No `HttpOnly` on a cookie that looks like a session or token (name such as `session`, `sid`, `auth`, `token`, `JSESSIONID`, `PHPSESSID`, `ASP.NET_SessionId`, `.AspNetCore.*`, or a JWT as its value).
- `SameSite` missing (browsers then use `Lax`; reported as Low), or `SameSite=None` without `Secure`.
- A session-looking cookie with a long lifetime (e.g. more than 30 days) or `Domain` set to a parent domain (shared with all subdomains).
- `__Host-` / `__Secure-` prefixes used wrongly.

**Response headers** (the page's main document; for API responses where noted):

- `Content-Security-Policy` missing, or weak: `unsafe-inline` or `unsafe-eval` in `script-src`, wildcard or `http:` sources, no `object-src`/`base-uri` restriction. Report-only policy shown as such.
- `Strict-Transport-Security` missing on HTTPS, or `max-age` under one year.
- Clickjacking: neither `X-Frame-Options` nor CSP `frame-ancestors`.
- `X-Content-Type-Options: nosniff` missing (also on API responses).
- `Referrer-Policy` missing or `unsafe-url`.
- Version disclosure: `Server` with a version, `X-Powered-By`, `X-AspNet-Version`, `X-AspNetMvc-Version` (Info/Low).
- CORS on API responses: `Access-Control-Allow-Origin: *` together with credentials, or the request's `Origin` echoed back with `Access-Control-Allow-Credentials: true`.
- Responses to authenticated requests (with `Authorization` or session cookies) without `Cache-Control: no-store` (Low).
- `Cross-Origin-Opener-Policy` and `Permissions-Policy` missing (Info).

**Page and transport:**

- The page itself on plain HTTP; mixed content (HTTP resources on an HTTPS page); a form or a password field that sends to HTTP.
- Secrets in URLs: tokens, session ids, API keys or passwords in query strings of the page or its requests (they end up in logs and history).
- Scripts and styles from other sites without Subresource Integrity (Info).
- A JWT stored in local or session storage (Info: readable by any script on the page).

## Acceptance criteria

- [ ] The dev tools dock has a **Security check** tab; it checks the current web page without sending any request of its own (checked with a network log).
- [ ] With dev tools turned on after the page loaded, the tab offers **Reload and check**; after it, the findings appear.
- [ ] A test page (`docs/testing/pages/`) served with no security headers and with cookies `session=abc` (no flags) and `theme=dark` gets findings for: no CSP, no HSTS (over HTTPS), no clickjacking protection, no `nosniff`, no `Referrer-Policy`; `session` without `Secure`, `HttpOnly` and `SameSite`; `theme` only without `Secure` and `SameSite`.
- [ ] A test page with good headers (strict CSP with `frame-ancestors`, HSTS of one year, `nosniff`, `Referrer-Policy: strict-origin-when-cross-origin`) and a `__Host-session` cookie with `Secure; HttpOnly; SameSite=Lax; Path=/` has no High or Medium findings, and the checks are listed as passed.
- [ ] A CSP with `script-src 'self' 'unsafe-inline'` is reported as weak; a report-only policy is shown as report-only.
- [ ] `Server: nginx/1.25.3` and `X-Powered-By: Express` are reported as version disclosure.
- [ ] An API response from the page's own site with `Access-Control-Allow-Origin` echoing the request's `Origin` and `Access-Control-Allow-Credentials: true` is reported; the same from another site is not checked.
- [ ] A request whose query string contains `access_token=` or `password=` is reported; mixed content on an HTTPS page and a form posting to `http:` are reported.
- [ ] Each finding shows what was found, why it matters, how to fix it and an OWASP link; **Copy as Markdown** copies them with the page's URL, with token values shortened.
- [ ] Hiding a finding for a site keeps it hidden after a reload and in a new tab of the same profile and site, shows "n hidden", and **Show hidden** brings it back; another profile still shows it.
- [ ] Works the same on macOS and Windows.

## Out of scope

- Active scanning: sending requests, fuzzing, injection or XSS attempts, spidering the site, brute force.
- Checking dependencies for known vulnerabilities (e.g. outdated jQuery).
- TLS configuration (ciphers, protocol versions) and certificate details: Chromium's Security panel shows the connection.
- A full OWASP ZAP or Lighthouse replacement.

## Configuration

The list of session cookie names and of secret query parameter names (with the defaults above), as with the secret headers of request export (006).

## Open questions

None.

## Notes

- 2026-11-05 (decided with the user): the page, the site's cookies and the Fetch/XHR responses of the page's own site are checked, not other sites; findings can be copied as Markdown; findings can be hidden per profile and site; nothing is shown outside the tab.

- 2026-11-05: requested by the user — a dev tools tab that checks the page for missing or wrongly set cookies and security headers (such as an unset CSP), like OWASP tools do, but a minimal security test, not a full scan.
- Comparable: OWASP ZAP's passive scan rules (cookie flags, CSP, HSTS, X-Content-Type-Options, anti-clickjacking, server version leaks, CORS), Lighthouse's Best practices (CSP effective against XSS, HSTS, COOP, clickjacking), securityheaders.com and Mozilla Observatory (headers only, on the public site). Chrome's DevTools has nothing like this: its Issues panel reports some cookie problems (SameSite, third-party cookies) and its Security panel the connection.
- Implementation hint: the data is already at hand — Requests records the tab's response headers (`src/core/request_log.h`, `response_headers`), Site data reads cookies with their flags, and storage is read for Tokens. The checks are pure functions over that data, in portable C++ or in the tools page's TypeScript, unit-tested with fixed inputs.
- 2026-11-05 (decided with the user, spec set to `ready`): the list of checks above is the first version; more can be added later. Severities follow OWASP ZAP's passive scan rules for the same issue (its risk rating); a check ZAP has no rule for gets the rating of its closest ZAP rule, recorded here when implemented. The severities written in the checks above (e.g. Low for missing `SameSite`) are examples and give way to ZAP's.
