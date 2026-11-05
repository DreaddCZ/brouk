# 029 – Security check in developer tools

- **Status:** draft
- **Priority:** P2
- **Depends on:** 006 (developer tools dock, Requests, Site data)

## Goal

Testers and developers often ship pages with missing security headers or carelessly set cookies: a session cookie readable by scripts, no `Content-Security-Policy`, no `Strict-Transport-Security`, a server announcing its version. Tools such as OWASP ZAP find these with a passive scan, but they need a proxy, set-up and a long report. Brouk should show the basic problems of the current page at a glance, in the dev tools, while testing it anyway — a minimal security check, not a penetration test.

## Description

A new tab **Security check** in the dev tools dock (006), next to Site data, Tokens, Requests, Rules and Utilities (027). (Chromium's DevTools has its own *Security* panel for the connection and certificate; this tab is named differently to avoid confusion.)

The check is **passive**: it only looks at what the tab already loaded — the responses recorded by Requests (006), the site's cookies and the page — and never sends a request or an attack of its own. If the tab's dev tools were turned on after the page loaded, the tab offers **Reload and check**.

Findings are listed by severity (**High**, **Medium**, **Low**, **Info**), each with a one-line title, what was found (the header or cookie, its value), why it matters, how to fix it, and a link to the matching OWASP cheat sheet. A summary at the top counts them ("2 high, 3 medium, 5 passed"); passed checks can be shown too, so the tester sees what was checked. **Check again** re-runs it after the next requests.

### Checks (first version, proposed)

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

To be written when the open questions are answered. Each check gets a criterion with a test page (`docs/testing/pages/`) that triggers it and one that passes it.

## Out of scope

- Active scanning: sending requests, fuzzing, injection or XSS attempts, spidering the site, brute force.
- Checking dependencies for known vulnerabilities (e.g. outdated jQuery).
- TLS configuration (ciphers, protocol versions) and certificate details: Chromium's Security panel shows the connection.
- A full OWASP ZAP or Lighthouse replacement.

## Configuration

Proposed: the list of session cookie names and of secret query parameter names (with the defaults above), as with the secret headers of request export (006).

## Open questions

1. Scope: only the page's main document and the site's cookies, or also every API (Fetch/XHR) response of the tab — and then only the page's own site, or third-party hosts too?
2. Is the proposed list of checks right for the first version? Anything to add (e.g. specific to the frameworks you test) or drop?
3. Should findings be exportable (Markdown for a bug report, or JSON), and should a finding be hideable per site ("known, accepted")?
4. Should the dock show that a page has High findings without opening the tab (e.g. a badge on the tab's name), or only inside the tab?
5. Severity levels: follow OWASP ZAP's ratings for the same issues, or our own?

## Notes

- 2026-11-05: requested by the user — a dev tools tab that checks the page for missing or wrongly set cookies and security headers (such as an unset CSP), like OWASP tools do, but a minimal security test, not a full scan.
- Comparable: OWASP ZAP's passive scan rules (cookie flags, CSP, HSTS, X-Content-Type-Options, anti-clickjacking, server version leaks, CORS), Lighthouse's Best practices (CSP effective against XSS, HSTS, COOP, clickjacking), securityheaders.com and Mozilla Observatory (headers only, on the public site). Chrome's DevTools has nothing like this: its Issues panel reports some cookie problems (SameSite, third-party cookies) and its Security panel the connection.
- Implementation hint: the data is already at hand — Requests records the tab's response headers (`src/core/request_log.h`, `response_headers`), Site data reads cookies with their flags, and storage is read for Tokens. The checks are pure functions over that data, in portable C++ or in the tools page's TypeScript, unit-tested with fixed inputs.
