# 007 – Ad and tracker blocking

- **Status:** done
- **Priority:** P1
- **Depends on:** 001, 003, 004, 005

## Goal

Block ads, trackers and annoyances natively, as well as uBlock Origin does, without extensions. Domain blocking works like a Pi-hole on the router, but inside the browser and per site. Pages stay fast and private, and internal work tools are never broken.

## Description

### Engine

- **Brave's `adblock-rust`** is the filter engine, used as a library through a C interface.
- It understands the **Adblock Plus and uBlock Origin filter syntax**: network rules, cosmetic (element hiding) rules including procedural ones, scriptlet injection, redirect resources, `$important`, `$badfilter` and exceptions.
- It also reads **hosts-file and plain domain lists** — the lists Pi-hole and DNS blockers use.
- **Matching** runs in the browser process, before each request is sent (CEF resource request handler on the IO thread).
- **Cosmetic rules and scriptlets** are applied by an injected script in the renderer (001), at document start, in every frame.
- **Redirect and scriptlet resources** come from Brave's `adblock-resources` (MPL-2.0), bundled with the app and updated with the lists.
- **Startup:** the compiled engine is cached on disk, so blocking is ready without re-parsing the lists.

### Filter lists

Lists are **global**: subscribed once, shared by all profiles. Each profile chooses which categories it uses (see Profiles below).

| Category | Lists | Default |
|---|---|---|
| Ads | EasyList, uBlock filters, uBlock filters – Quick fixes, uBlock filters – Unbreak | on |
| Privacy | EasyPrivacy, uBlock filters – Privacy, URL tracking parameters (removes `utm_` and similar from links) | on |
| Malware | uBlock filters – Badware risks, Malicious URL blocklist | on |
| Domain lists (DNS-style) | Hagezi Multi Normal, OISD small, StevenBlack hosts | off |
| Cookie banners | EasyList Cookie, uBlock filters – Cookie notices | on |
| Other annoyances | Fanboy's Annoyance list (social widgets, newsletter pop-ups, "open in app" banners) | off |
| Regional | EasyList Czech and Slovak | on (chosen from the system language) |

- **Lists from URL:** the user can add any list by URL, in any supported format, into a category or "Custom".
- **Updates:** lists update in the background according to each list's `! Expires:` header, and at most every 6 hours. Lists without the header update every 4 days. "Update now" is in settings.
  - A list that fails to download or parse keeps its previous version, with the error shown in settings.
- **Blocking a domain** blocks every request to that domain and its subdomains, including the page itself. A blocked page shows an error page saying which list blocked it, with "Allow this site" and "Go back".

### Own rules

- **My filters:** a text editor in settings for the user's own rules, in the same syntax, with syntax errors marked. They apply to all profiles, and override the lists.
- **Element picker:** right-click on a page → **Block element…** (also in the blocker popup). Hovering highlights elements. The picked element can be widened to its parent, and the generated cosmetic rule (`site##selector`) can be edited before saving to My filters. **Preview** hides the element until confirmed.
- **Domain rules from the popup:** for each blocked or allowed third-party domain on the page, the user can add an allow or block rule, just for this site or everywhere. This is a simple form of uBlock's dynamic filtering.

### Toolbar button and popup

- A **shield button** next to the address bar shows the number of requests blocked on the current page. It is dimmed when blocking is off for the site.
- **The popup shows:**
  - a big **on/off switch** for the current site, saved to the active profile's allowlist;
  - counts of what was blocked (ads, trackers, other);
  - the list of **third-party domains** the page contacted, with the number of requests to each, and allow/block buttons (see Own rules);
  - "Block element…", "Report a broken site" (opens the list maintainers' issue page with the URL filled in) and "Blocker settings".
- **Blocked requests list:** "Show blocked requests" in the popup opens a list of this tab's requests that were blocked or allowed by an exception, with the matching rule and its list. Developers can use it to find out why something broke.

### Profiles

Lists are global; these choices are **per profile**:

- blocking **on or off** for the whole profile (on by default);
- the **allowlist** — sites where blocking is off;
- which **categories** apply: cookie banners on and other annoyances off by default, each switchable per profile, and likewise the other categories.

For example, a work profile can turn blocking off on its intranet hosts, or entirely. A personal profile can add the annoyances and domain lists.

**Never blocked:**
- `brouk://` pages;
- `localhost`, private network addresses (10.x, 172.16–31.x, 192.168.x, `.local`) and the hosts of sites in routing rules (010), unless a filter names them explicitly. Company tools and local development are not broken by broad list rules.

### Secure DNS (DNS-over-HTTPS)

- In settings, Brouk can send DNS lookups to a DNS-over-HTTPS service: off (system DNS, default), or a provider from a list — Cloudflare, Quad9, AdGuard DNS (blocks ads), NextDNS (own configuration), Control D — or a custom template.
- Blocking providers block on DNS level, as a Pi-hole does.
- Settings warn that company VPN and intranet names often only resolve through the company's DNS, and may stop working.
- **Scope:** per profile if CEF allows it, otherwise app-wide (see Notes). If it is app-wide, settings say so.

## Acceptance criteria

**Blocking**
- [x] With default lists, ads and trackers are blocked on common test pages (e.g. the EasyList and uBlock test pages, a news site); ad placeholders are hidden.
- [x] uBlock Origin syntax works: network rules, exceptions, `$important`, cosmetic and procedural cosmetic rules, scriptlets and redirect resources.
- [x] Domain lists (Hagezi, OISD, hosts files) can be enabled and block every request to listed domains and subdomains. A blocked page explains which list blocked it and offers "Allow this site".
- [x] Cookie banners are hidden by default; other annoyances are off by default; both can be switched per profile.

**Lists and own rules**
- [x] Lists can be enabled, disabled, added by URL and removed. They update by their `Expires` header (at most every 6 hours, default 4 days); a failed update keeps the previous version and shows the error.
- [x] My filters accepts rules in the same syntax, marks invalid ones and applies to all profiles.
- [x] The element picker creates a cosmetic rule for the picked element, editable before it is saved, with a preview.
- [x] From the popup, a third-party domain can be allowed or blocked for this site or everywhere.

**Popup and profiles**
- [x] The shield shows the blocked count of the current page; its popup turns blocking off for the site with one click, saved per profile.
- [x] The blocked requests list shows each blocked or excepted request with its rule and list.
- [x] Blocking can be turned off for a whole profile; profiles have separate allowlists and category choices.
- [ ] Localhost, private network addresses and hosts in routing rules are not blocked by list rules that do not name them. (after 010)

**Secure DNS and performance**
- [x] Secure DNS can be set to a listed provider or a custom template, is off by default, and warns about VPN and intranet names.
- [x] Blocking adds no noticeable page-load overhead: matching a request takes well under 1 ms. Starting with blocking ready takes no longer than without the engine plus 200 ms (cached engine).

## Out of scope

- Running uBlock Origin or any other extension (001).
- YouTube-specific ad blocking beyond what the filter lists do.
- CNAME uncloaking (CEF exposes resolved addresses, not CNAME chains).
- uBlock's full dynamic filtering matrix and its advanced settings.
- Blocking for apps other than Brouk (that remains the router's or Pi-hole's job).

## Configuration

| Setting | Default | Scope |
|---|---|---|
| Subscribed lists and categories | see the table above | global |
| My filters | empty | global |
| List update | by list headers, at most every 6 h, fallback 4 days | global |
| Blocking on | on | per profile |
| Allowlist | empty | per profile |
| Categories used | cookie banners on, other annoyances off, domain lists off, others on | per profile |
| Secure DNS | off | per profile if possible, else global |

## Open questions

None.

## Notes

- 2026-10-21 (decision in 001): Chrome extensions are not supported, so this built-in blocker is the only ad blocking; it should cover what uBlock Origin does (network and cosmetic filtering, uBO filter syntax). Engine choice: `adblock-rust` conflicts with the "no Rust" language decision unless used as a prebuilt library through its C interface (adds a Rust toolchain to the build); the alternative is our own C++ engine. Decide when this spec is detailed.
- 2026-10-23 (adblock review with the user):
  - The user wanted uBlock plus own options, and asked about router-style DNS blocking.
  - **Engine:** adblock-rust through its C interface.
  - **Licences:** this is an approved exception to the "no Rust" rule and to the licence list. The Rust toolchain is used only to build this library; it is pinned and installed by `scripts/install_tools`. The library is MPL-2.0, as are Brave's `adblock-resources`. `CLAUDE.md` and 001 were updated.
  - **Settings:** lists are global; blocking on/off, the allowlist and categories are per profile. Cookie banners are on by default and other annoyances off, both switchable per profile.
  - **DNS:** domain lists (Hagezi, OISD, StevenBlack) work in the blocker, and Secure DNS is an option.
  - Spec set to `ready`.
- Check before implementing:
  - **adblock-rust interface:** its current C or C++ interface (Brave's `adblock-rust-ffi` or the bindings in `brave-core`). If no maintained C ABI exists, a thin wrapper crate that exposes one is acceptable; it is the only Rust code we write.
  - **Secure DNS scope:** in Chromium, secure DNS is configured for the whole network service (local state), not per profile. Check whether CEF allows a different setting per request context. If not, Secure DNS is app-wide, and per-profile DNS-style blocking is done with the domain lists.
  - **Injection timing:** that scriptlets can be injected before page scripts in every frame (`CefRenderProcessHandler::OnContextCreated`), and how procedural cosmetic filters returned by the engine are applied.
- Default lists are downloaded at runtime from their publishers, not bundled (except a first copy for offline first start, where licences allow).
- 2026-10-23 (implementation, engine): Brave's C interface (`adblock-rust-ffi`) is archived, so `src/adblock` is our thin wrapper crate around `adblock` 0.13.3 with a C ABI (`src/adblock/include/brouk_adblock.h`), and `src/core/adblock_engine.h` wraps it in RAII C++ (portable, tested on Linux too). Rust 1.98.0 is pinned in `scripts/install_tools` (standalone installers, SHA-256 checked, installed into `.tools`; Cargo's downloads go to `.tools/cargo`); `Cargo.lock` pins every crate with its checksum. The crate is always built in release mode with `panic = "abort"`, and every exported function also catches panics, so nothing unwinds into C++. Engines are built in debug mode so each match tells its list and line (for the blocked requests list). Matching is thread-safe (adblock-rust without its single-thread feature). uBlock's own lists and My filters will be "trusted" (may use scriptlets needing trust), as in uBlock Origin. Measured: about 35 µs per request against 40,000 rules in a sanitizer debug build.
- 2026-10-24 (implementation, lists and blocking): the lists of the 007 table are built in (`src/core/adblock_lists`), downloaded from their publishers into `adblock/lists/` (not bundled: the first start blocks once the downloads finish, about 15 seconds after launch), with their state in `adblock/lists.json`. Updates follow each list's `! Expires:` header, at most every 6 hours, otherwise every 4 days; checked 15 seconds after start and then hourly. A failed download keeps the previous version and records the error; an answer that is not a list (an HTML page) counts as failed. The regional list is on for Czech and Slovak system languages. Settings: `adblock.lists_on` / `adblock.lists_off` (changes from the defaults), `adblock.custom_lists` (category=URL entries), per profile `adblock.enabled`, `adblock.allowlist` (URL patterns as in 006) and `adblock.categories`. One engine is built per set of categories the profiles use (lists of those categories + My filters, which are "trusted" for scriptlets like uBlock's own lists), cached in `adblock/engines/`. Requests are checked on the IO thread in each tab's request handler; service workers and requests without a tab are not filtered yet. A blocked page load is cancelled and a native view explains it (list and rule) with "Allow this site" (adds the host to the profile's allowlist) and "Go back"; a web page cannot trigger the allowlisting. "Never blocked" hosts (localhost, private addresses, .local; routing hosts follow with 010) are blocked only by a rule whose text contains the host. Redirect resources come from Brave's adblock-resources, bundled at a pinned commit and updated with the lists; pages and frames are never replaced by a redirect.
- 2026-10-24 (implementation, cosmetic filtering): each frame's page script asks the browser for its cosmetic resources when the frame's script context is created; the answer brings hiding selectors (injected as a style), procedural filters (has-text, matches-css(-before/-after), matches-attr, matches-path, min-text-length, upward, xpath; actions remove, style, remove-attr, remove-class) and the scriptlets, which the renderer evaluates in the page. CEF has no synchronous renderer-to-browser call, so this answer arrives shortly after the page's first scripts start: element hiding is in place before the page shows, but a scriptlet can come too late for code that runs at the very start of a page. Generic hiding by classes and ids works as in Brave: the page script reports classes and ids (also of later changes) and the browser answers with generic selectors, unless `$generichide` applies.
- 2026-10-24 (implementation, shield, popup, picker, settings, DNS): the shield sits left of the star with the page's blocked count; dimmed with a slash when blocking is off for the site or the profile. The popup's domain rules are written to My filters in list syntax: `||domain^$important` (block everywhere), `||domain^$important,domain=site` (block on this site), `@@||domain^` and `@@||domain^$domain=site` (allow). "Report a broken site" opens the issue form of the first list that blocked something on the page (uBlock's form otherwise). Counts: ads = Ads and Regional lists, trackers = Privacy lists, other = the rest and My filters. The element picker runs in the page's main frame; a picked selector is accepted only during a picker the user started, and the rule (`site##selector`) is shown in a native dialog for a last edit before it is saved. Filter lists and My filters are managed in Settings (a panel above the per-profile settings); My filters lives in `adblock/my-filters.txt` rather than settings.yaml (it can be long); invalid lines are reported with their numbers while typing. Secure DNS (`dns.secure`, `dns.custom_template`) is app-wide: Chromium configures DNS over HTTPS for the whole network service (local state `dns_over_https.mode` = secure and its template), so CEF cannot set it per profile; per-profile DNS-style blocking is done with the domain lists. The setting says it applies to all profiles and warns about VPN and intranet names.
- 2026-10-24 (user's decision): tested and accepted; the criterion that includes the hosts of routing rules is marked "(after 010)" (localhost and private addresses already work). Spec set to `done`.
