# 007 – Ad and tracker blocking: manual test checklist

The first start downloads the lists (about 15 MB) about 15 seconds after launch; blocking starts once they are there. Settings → "Ad blocking: filter lists" shows each list's state.

## Blocking

1. Open https://d3ward.github.io/toolz/adblock (or https://canyoublockit.com/testing/) and a news site with ads. Expected: most tests pass / no ads; the shield next to the star shows a number (requests blocked on this page).
2. Open https://ublockorigin.github.io/uBlock-tests/ (or the EasyList test pages). Expected: network rules, exceptions, element hiding and procedural rules work.
3. A page using a script blocked with a `$redirect` (e.g. Google Analytics on a news site). Expected: the page still works (a neutral script is served instead).
4. A link with `utm_source=…` parameters. Expected: the page opens without them (URL tracking parameters list).
5. Cookie banners on a typical EU news site. Expected: hidden by default.
6. Settings → For one profile → Kinds of lists used: add `annoyances` (and/or `domains`). Expected: newsletter pop-ups / social widgets are hidden after reload; with `domains` the domain lists apply (turn them on in the list section first, e.g. OISD small).
7. My filters: add `||example.com^` and open https://example.com. Expected: "Brouk blocked this page" with the list "My filters" and the rule; "Allow this site" opens it (and adds it to the profile's allowlist); "Go back" closes the message.
8. Open http://localhost:… or a 192.168.x.x device page. Expected: not blocked by broad rules.

## Shield and popup

9. Click the shield. Expected: the site with a switch, counts (ads, trackers, other), third-party domains with request counts (blocked in red), Block element…, Show blocked requests, Report a broken site, Blocker settings.
10. Switch blocking off for the site. Expected: the page reloads unblocked, the shield is dimmed (shield with a slash); the site is in Settings → Sites where blocking is off; switching on again removes it.
11. A third-party domain's Rule menu → Block on this site / Block everywhere / Allow…. Expected: a rule is added to My filters and the page reloads accordingly.
12. Show blocked requests. Expected: a window listing blocked (red) and allowed-by-exception (green) requests with type, address, rule and list.
13. Report a broken site. Expected: a new tab with the list maintainers' report form, the page address filled in.

## Element picker

14. Right-click on a page → Block Element… (or Block element… in the popup). Expected: elements highlight under the mouse; a click picks one; the panel shows a selector; Parent widens it; Preview hides the element; Create shows "Block this element?" with the rule `site##selector`, editable; Block saves it and the element stays hidden after reload. Esc or Cancel ends the picker.

## Lists and My filters

15. Settings → Ad blocking: turn a list off and on. Expected: saved; the change applies to new page loads.
16. Add a list by address (e.g. https://raw.githubusercontent.com/hagezi/dns-blocklists/main/adblock/light.txt). Expected: listed under its kind with "Not downloaded yet", then "Updated just now"; Remove deletes it.
17. Add an address that is not a list (e.g. https://example.com/). Expected: after the download the list shows the error "The address did not return a filter list."
18. Update now. Expected: "Updating…", then the lists show new times.
19. My filters: type `||bad^$nonsense`. Expected: "Line n: …" below the editor (also before saving); Save keeps the valid rules.

## Profiles

20. Turn "Block ads and trackers" off for one profile. Expected: no blocking and a dimmed shield in its tabs; other profiles still block.
21. Two profiles with different allowlists and kinds of lists. Expected: each tab follows its own profile.

## Secure DNS

22. Settings → Secure DNS → Cloudflare (or AdGuard DNS). Expected: pages load; with AdGuard DNS, ad domains do not resolve even with blocking off. Custom with an https address works; the description warns about VPN and intranet names. Off returns to the system DNS.

## Performance

23. Quit and start Brouk. Expected: no noticeable delay; blocking works on the first page (the engine comes from the cache).
