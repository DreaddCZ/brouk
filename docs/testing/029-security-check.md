# 029 – Security check: manual test checklist

Run on macOS and on Windows. Start the test pages with `node docs/testing/pages/security_server.mjs` (Node is in `.tools/` or installed); they are at `http://127.0.0.1:8794/bad` and `/good`.

1. Open `http://127.0.0.1:8794/bad` with dev tools off. Turn them on and choose the dock's **Security check** tab. Expected: a notice that the page loaded before the dev tools were on, with **Reload and check**; cookie and storage findings are already listed.
2. Press **Reload and check**. Expected: after the reload, Medium: no Content-Security-Policy, no protection against clickjacking, a script from another site without integrity; Low: `session` without Secure, HttpOnly and SameSite, `theme` without Secure and SameSite (not HttpOnly), `__Host-wrong` breaks its prefix, no HSTS, no nosniff (page and API), no COOP, no Permissions-Policy, `Server: nginx/1.25.3` and `X-Powered-By: Express`; Informational: no Referrer-Policy, API allows the requesting origin with credentials, signed-in responses may be cached, `password` in a URL (value shortened), JWT in local storage. A note says 127.0.0.1 is checked as if served over HTTPS.
3. Open a finding. Expected: what was found, why it matters, how to fix it, the ZAP rating, and **OWASP reference**, which opens the OWASP page in a new tab.
4. **Copy as Markdown** and paste into a text editor. Expected: the page URL, the time, findings grouped by severity; the JWT and the password are shortened (`eyJhbGci…`).
5. **Hide for this site** on "No Content-Security-Policy". Expected: it disappears, the summary says "1 hidden", **Show hidden (1)** shows it marked Hidden with **Show again**. Reload the page and **Check again**: still hidden. Open the page in a new tab of the same profile: still hidden. In another profile: shown.
6. Clear the site's data (Site data → Clear site data), open `http://127.0.0.1:8794/good` and **Reload and check**. Expected: no High or Medium findings; the passed checks list the CSP, HSTS, clickjacking, nosniff and cookie checks.
7. Open `https://example.com/` (served without security headers) and check it. Expected: no CSP, no HSTS, no clickjacking protection and the other header findings; no "checked as if HTTPS" note.
8. Open a site that sets cookies (e.g. a sign-in page you test). Expected: findings only about that site's own cookies and API calls, not about other sites it loads from.
9. With the Requests tab, watch the requests while pressing **Check again**. Expected: the check adds no request.
10. Settings → Developer tools: remove `session` from "Session cookie names" and check the bad page again. Expected: `session` is no longer reported without HttpOnly.
11. On a Brouk page (e.g. Settings) the dock is not available (internal pages have no dock, 006); on an empty tab the Security check says it works on web pages.
12. Keyboard: Tab reaches Check again, Copy as Markdown, each finding (Enter opens it), its link and Hide button.
