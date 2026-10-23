# Manual test checklist – 006 Developer tools

One pass over all of spec 006, in the order of its acceptance criteria.
Each step: what to do → what should happen. Steps marked **(after 003)** wait
for the command palette and shortcut settings.

Serve the test pages first (see [README](README.md)):

```sh
python3 -m http.server 8765 --bind 127.0.0.1 --directory docs/testing/pages
```

Have at least two profiles (Profiles → Manage Profiles…).

## Dock and DevTools

1. Open any site, click the hammer next to the address bar → DevTools opens at
   the bottom. Click it again → gone.
2. Dock header buttons: left, right, separate window, bottom → DevTools moves
   each time. Drag the divider to resize. Close and reopen → same position
   and size.
3. Open two tabs, dev tools on in the first only. Switch between them → the
   first shows its dock, the second has none. Pick "Site data" in the first,
   switch away and back → still on Site data.
4. In a window with tabs of two profiles, dev tools on in both → Application →
   Cookies (and Site data) show each tab's own profile's cookies.
5. Elements: select and edit a node. Console: `1+1` → `2`. Sources: set a
   breakpoint, reload → it stops; resume. Network: reload → requests listed.
   Performance: record a few seconds → a profile appears. Memory: take a heap
   snapshot → it appears. Application: storage of the site is listed.
6. Device toolbar (the phone/tablet icon in DevTools) → the page is shown at a
   phone size; turn it off → normal again.
7. Note which extra panels exist (⋮ → More tools: Lighthouse, Recorder, …)
   and whether they work, so the spec can record it.
8. Settings (⌘,) → right-click → **Inspect** → a "Developer Tools – Settings"
   window opens with the clicked element selected. ⌥⌘I there closes it.
   In Settings, ⌥⌘J → Console, ⌥⌘C → pick an element.
9. Profiles → Manage Profiles… → right-click → Inspect → DevTools for the
   profile manager.
10. In a tab's dock, choose "Site data" → right-click the tools page →
    Inspect → DevTools for the tools page itself.
11. **(after 003)** Remap the dock shortcuts in settings → the new keys work.

## Dev tools toggle

12. The hammer is always shown, to the right of the address bar. Settings →
    Developer tools → move it to the left → it moves; back to the right.
13. With dev tools on in a tab: ⌘-click a link → the new tab has dev tools on.
    A popup (e.g. a "Sign in with…" window) → dev tools on too. ⌘T → off.
14. Two tabs of the same profile in one window, both on. Turn one off → the
    other stays on.
15. The hammer is filled (accent colour) on a tab with dev tools on and plain
    on one with them off, switching tabs updates it.
16. Turning on opens the dock. Turning off closes it. Close the dock with its
    × → the hammer turns off. Undock into a window, close that window → the
    hammer turns off.
17. On a tab with dev tools off: ⌥⌘I → on. ⌥⌘J → on, Console shown. ⌥⌘C →
    on, hovering highlights elements. Right-click → Inspect → on, the element
    is selected in Elements. **(after 003)** command palette → "Developer
    tools" → on.
18. Dev tools on, navigate to another site and back → still on.

## Site data

19. Open `localhost:8765/profiles.html`, press "Remember". Dev tools → Site
    data → the `brouk_test` cookie and local storage entry are listed.
20. Edit the local storage value → Save → reload: the page shows the new
    value. Add a cookie → appears; delete it → gone. Same for session storage.
21. On a site using IndexedDB or cache storage (e.g. a signed-in web app):
    entries are listed; Delete removes one.
22. Open `localhost:8765/service-workers.html`, press "Register". Site data →
    Refresh → "Service workers" lists `http://localhost:8765/` and
    `…/second/`, each with `/sw.js · activated, running`. Delete one →
    "Show registrations" on the page lists only the other.
23. Open `profiles.html` in a second profile, press "Remember" there. In the
    first profile: Clear site data… → Delete → after reload the first profile
    shows "(none)"; the second profile still has its data; other sites in
    the first profile keep theirs.

## Tokens

24. Open `localhost:8765/requests.html`, press "Store a token…", dev tools on,
    press the request buttons. Tokens → JWTs from the request headers, the
    cookie and local storage are listed, decoded (issuer, subject, scopes …).
25. A response header token (if the page sends one) is listed too.
26. Times show local time and relative time ("expires in …", "expired … ago");
    the expired token is marked Expired.
27. "Signature not verified" is shown. A pasted non-JWT bearer token is
    listed as not decodable. Paste a real token from work → decoded.

## Requests and export

28. Requests lists the page's requests (Fetch/XHR by default). Filters: type
    All, method POST, status Failed, text "login" → each narrows the list.
29. Click a request → headers and request body; "Load" the response body →
    a JSON body is shown as a tree (Tree / Raw / Pretty, search, copy value
    and path); a non-JSON body as text.
30. Select all → Export to Postman… with the environment file → import both
    files into Postman and into Bruno → method, URL, query parameters,
    headers and body are kept; `{{authorization}}` and `{{baseUrl_…}}`
    resolve from the environment.
31. Export without the environment file → open the collection in a text
    editor: no token or cookie values in it.

## Request rules

32. Open `localhost:8765/rules.html`, dev tools on, Rules: add the four
    example rules from the page → listed.
33. Press "Run requests" → blocked/x.json fails; old.json is redirected to
    sample.json; the responses have `X-Brouk-Rule: works`; Requests shows
    `X-Env: staging` in the request headers. The hammer turns orange, its
    tooltip says rules changed the page.
34. Untick the block rule → blocked/x.json reaches the server (404). Tick
    "Pause all" → nothing is changed. Untick it.
35. Edit `~/Library/Application Support/Brouk/profiles/<id>/request-rules.yaml`
    by hand → within 2 seconds the Rules tab shows the change and it applies.
36. Turn dev tools off, press Run requests → nothing is changed; reload → the
    hammer is no longer orange.
37. A tab of another profile with dev tools on → its Rules tab is empty and
    the first profile's rules do not apply there.

## JSON viewer

38. Open `localhost:8765/sample.json` → tree. Collapse and expand; Raw and
    Pretty; search "item" with ↑ ↓; hover a line → Copy value, Copy path.
39. `json.html` → "Open 10 MB JSON" → shown within seconds, the window stays
    responsive; search "item 99999".
40. Settings → Pages → turn the JSON viewer off → reload → plain text. On
    again.

## Remote debugging

41. By default `curl http://127.0.0.1:9222/json/version` gets no answer and no
    indicator is shown.
42. Settings → Developer tools → remote debugging on, restart → every window
    shows "Remote debugging :9222" with the all-profiles warning in its
    tooltip; the curl answers. From another machine on the network (or
    `curl http://<this Mac's IP>:9222/json/version`) → no answer.
43. Turn it off, restart. Start Brouk with `--remote-debugging-port=9333`
    (see docs/building.md) → indicator with :9333.
