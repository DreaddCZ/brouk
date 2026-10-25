---
marp: true
title: Why Brouk
paginate: true
---

<!-- Slides are separated by "---". The file renders as a deck with Marp (VS Code extension or `npx @marp-team/marp-cli`) and reads fine as plain Markdown. Speaker notes are in HTML comments. -->

# Brouk 🪲

### A browser built for how we actually work

Many client accounts in one window · full control over every page · nothing you don't need

<!-- Brouk is Czech for "beetle" — a browser made by someone who hunts bugs for a living. Built on Chromium, so sites work exactly as in Chrome. -->

---

## Sound familiar?

- Three Microsoft accounts, two Azure tenants, a client Jira and our own — and Chrome keeps logging you into the wrong one
- Five Chrome windows, one per profile, and no idea which window is which client
- A link from Teams opens in the wrong profile, so you copy it, switch windows, paste
- Private windows as a "second account" — and you log in again every single time
- Testing an API means DevTools, a JWT site in another tab and retyping the request in Postman

<!-- Ask the room: who has more than two Microsoft logins open right now? -->

---

## The idea

**Brouk is Chromium with a work-first shell.**

- Same engine as Chrome: sites, sign-ins, Teams, Azure portal and DevTools work the same
- Local profiles that live **side by side in one window**
- Links that go to the **right profile by themselves**
- Developer tools for **testing**, not just debugging
- No sign-in, no news feed, no promotions, nothing sent anywhere

---

## Many accounts, one window

- A **profile** is a fully separate browser: own cookies, logins, storage, history and passwords
- Tabs of several profiles share **one window** — each profile is a coloured, collapsible tab group
- `Client A` (blue) · `Client B` (green) · `Internal` (grey), right next to each other
- Logging into Azure in Client A never touches Client B
- No Google or Microsoft account needed for a profile — it is local only

**Chrome:** one profile per window · **Firefox containers:** separate cookies, but shared history and settings · **Brouk:** full isolation, same window

<!-- This is the main reason the browser exists. Demo: open portal.azure.com in two groups side by side, each signed into a different tenant. -->

---

## Links open in the right profile

- Simple rules: `*.atlassian.net/clientA` → Client A, `github.com/our-org/*` → Internal
- One profile in the rule → it just opens there. Several → a quick picker (press `1`, `2`, `3`)
- Works for links from **Outlook and Teams**, including **Safe Links**, Proofpoint and Mimecast wrappers — routed by the real address, while the company link check still runs
- Several windows? The picker shows thumbnails of each window and its profile groups
- Sign-in redirects and popups never jump profiles, so logins don't break

<!-- Demo: click a Jira link in Teams; it lands in the right client's group without touching the clipboard. -->

---

## Full control over the page

Chromium DevTools, plus tools we usually open five other sites for — in **one dock**:

| Tool | What it does |
|---|---|
| **Site data** | View and **edit** cookies, local/session storage, IndexedDB; clear a site in one click |
| **Tokens** | Finds JWTs in headers, cookies and storage; decodes them; "expires in 12 min" |
| **Requests** | Filtered list of the tab's API calls |
| **Request rules** | Add, change or remove headers, redirect or block — live, per profile |
| **JSON viewer** | Any JSON response as a searchable, collapsible tree |

Dev tools are switched **per tab** with an always-visible toggle — rules never leak into normal browsing.

<!-- Request rules: e.g. add a feature-flag header, swap the API host for a local one, block a tracking script — without a proxy or an extension. -->

---

## From the browser to Postman or Bruno in seconds

- Select requests → **Export** → a Postman v2.1 collection that Postman and Bruno both import
- Method, URL, headers and body kept; grouped by host
- **Secrets become variables:** `Authorization`, cookies and API keys go into a separate environment file — or nowhere
- Hosts become `{{baseUrl}}`, so the same collection runs against test, staging or production
- **Remote debugging** for Playwright and Puppeteer: off by default, localhost only, clearly shown when on

<!-- Typical case: reproduce a bug a client reported. Click through it once, export, hand the collection to a colleague — no tokens leaked in Slack. -->

---

## Passwords and company tools

- Each profile chooses its password source: **built-in** or the company **Bitwarden** vault (through the official CLI)
- Exact-origin filling, a password generator, and **2FA codes** — even read from the QR code on the page
- **Autofill** made for us: Czech/EU company fields (IČO, DIČ, VAT, IBAN), custom fields, and no duplicate addresses
- One shared **bookmark** tree; a bookmark can belong to a profile and only show there

---

## Quiet, fast, and yours

- **Ad, tracker and cookie-banner blocking** built in (Brave's engine), allowlist per profile
- **History you can search properly:** by host, with filters, even with read-only SQL — and a clean-up that keeps only what you choose
- **Session restore,** command palette, every shortcut remappable
- Tabs that never get so small you close the wrong one
- Settings in plain YAML files: back them up, share them, version them
- Updates itself automatically

---

## Coming later: a VPN for one profile

- Connect a profile to a client's **FortiClient, Cisco or OpenVPN** network — only that profile
- Your Mac, other apps and other profiles stay on the normal network
- No admin password, no vendor client app, single sign-on in a Brouk window
- If the VPN drops, the profile **stops** instead of leaking to the internet

*Being investigated — depends on what our company computers allow.*

---

## Brouk vs. Chrome vs. Firefox

| | Chrome | Firefox | **Brouk** |
|---|---|---|---|
| Several accounts in one window | ✗ one profile per window | ~ containers, shared history | ✓ isolated profiles, one window |
| Links from Teams/Outlook to the right account | ✗ | ~ add-on | ✓ rules, Safe Links aware |
| Edit cookies and storage | ~ DevTools only | ~ DevTools only | ✓ Site data tool |
| JWT decoding, header rules | ✗ extensions | ✗ add-ons | ✓ built in |
| Export to Postman/Bruno, secrets removed | ✗ HAR only | ✗ HAR only | ✓ |
| Account sign-in, promotions, telemetry | yes | some | none |
| Chrome extensions | ✓ | own add-ons | ✗ |

---

## Honest limits

- **macOS first** (Apple Silicon and Intel); Windows later
- **No Chrome extensions.** Most of what we install them for is built in; Bitwarden works through its CLI
- **No passkeys in iCloud Keychain** until the app is signed (security keys work)
- **No sync** between computers — profiles and settings are exported and imported as files
- A young project: built and tested by one person so far — your bug reports shape it

---

## Where it stands

| Area | Status |
|---|---|
| Browsing, tabs, profiles side by side, session restore | ✅ in use |
| History, bookmarks, ad blocking | ✅ in use |
| Developer tools | 🔧 being built |
| Passwords (built-in manager) | 🔧 built, in testing; Bitwarden next |
| Form autofill (Czech and EU company details) | 🔧 built, in testing |
| Link routing, downloads | 📋 specified, next |
| VPN per profile | 🔍 being investigated |

<!-- Update this slide before each presentation. -->

---

## Try it

- Early builds for colleagues come with **signed releases and automatic updates**
- Bring your worst case: the most accounts, the most clients, the most tabs
- Tell us what breaks and what is missing — features are specified together with the people who use them

**Questions?**
