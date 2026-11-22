## Link routing {#link-routing}

Brouk opens links in the right profile by **rules**: a URL pattern and the profile (or profiles) its links belong to. For example, `*.atlassian.net` → *Work*, or `portal.azure.com` → *Client A* or *Client B*.

**Which links are routed:**

- links from other apps (mail, chat, documents) — when Brouk is the [default browser](#link-routing-default-browser);
- new tabs and windows you open from a page: {key:mod}-click, middle-click, "Open Link in New Tab/Window", links that open a new tab;
- addresses typed into a **new** tab.

**Never routed:** navigation inside a tab, redirects, and sign-in popups. They stay in their profile, so sign-ins (Microsoft, Google, GitHub) keep working.

**Without a matching rule:** a link from inside Brouk stays in its tab's profile; a link from another app opens in the default profile, in the most recently used window on the current screen.

Rules are edited in **Settings → Links**, or per profile in the profile manager and Settings → Profiles ("Links that open in this profile").

### Writing rules {#link-routing-patterns}

A rule's pattern uses the [URL pattern](#url-patterns) syntax:

| Pattern | Matches |
|---|---|
| `companya.atlassian.net` | that host |
| `*.atlassian.net` | every subdomain of `atlassian.net` |
| `github.com/company-a/*` | pages under that path — for services where all accounts share one host |
| `portal.azure.com/#@contoso.onmicrosoft.com/*` | the part after `#` too: Azure portal links of one tenant only |

The **most specific** rule wins: a longer path beats a shorter one, an exact host beats a wildcard. A rule with **one** profile opens the link there at once; a rule with **several** profiles asks with the picker.

A fragment pattern (after `#`) is compared as written, including case. Azure links without a tenant (such as `portal.azure.com/#home`) don't match it.

### The picker {#link-routing-picker}

When a rule lists several profiles, or the profile's tabs are in several windows, a picker appears in the middle of the screen:

1. **Profile:** the rule's profiles with their number keys <kbd>1</kbd>–<kbd>9</kbd>. Tick **"Always open links matching … in this profile"** (or press <kbd>A</kbd>) to make the rule use only the chosen profile from now on; you can change it back in Settings → Links.
2. **Window** (only when that profile's group is in several windows): each window with a thumbnail, its active tab and its profile groups; the last entry is **New window**.

Number keys choose at once; <kbd>↑</kbd> <kbd>↓</kbd> and {key:return} work too. <kbd>Esc</kbd> goes back a step, or cancels — the link is then not opened.

**Which window:** a profile's group in exactly one window → that window; in several → the picker asks; in none → a new group in the front window. {mac}When the chosen window is on another desktop (Space), macOS switches there, or — Settings → Links → "When the chosen window is on another Space" — the window moves to you.{/mac}{win}When the chosen window is on another virtual desktop, Windows switches to that desktop, or — Settings → Links → "When the chosen window is on another desktop" — the window moves to yours.{/win}
{win}

The picker shows a live picture of each window, as the taskbar does (a minimised window shows a frame instead).
{/win}

### Safe links and other wrappers {#link-routing-wrappers}

Links in mail and chat often go through a security service first (Microsoft Safe Links, Proofpoint, Mimecast…). Brouk always routes by the **real** address, never the wrapper's, and the picker says "via Safe Links" and similar.

- **Built in:** Outlook and Teams Safe Links, Proofpoint URL Defense, Barracuda, Cisco, Google redirect links, and Mimecast.
- Where the real address is in the link itself, Brouk reads it, routes by it, and still opens the wrapper link in the chosen tab, so your company's check runs and is logged.
- Where it is not (e.g. Mimecast), Brouk follows the link in a temporary, empty context that belongs to no profile. If the service shows a page of its own (a warning or a "continue" button), it appears in a small **Checking link** window; once you continue, the link is routed. Closing that window cancels the link.
- Add your company's own wrapper in Settings → Links: its host and either the name of the URL parameter that holds the real address, or "opaque".

### Default browser {#link-routing-default-browser}

Links from other apps reach Brouk only when it is the default browser: **Settings → Links → Make Brouk the default browser** {mac}(macOS asks you to confirm).{/mac}{win}— it registers Brouk as a browser for your Windows account and opens Windows Settings → Default apps → Brouk. Choose Brouk there for HTTP and HTTPS (Windows lets no app do this by itself). If you move the Brouk folder, it updates its registration at the next start.{/win}

### Why did a link open there? {#link-routing-why}

{mac}Every routed link is noted in the macOS log. Open **Console**, search for `Link routing` and process `brouk`.{/mac}{win}Every routed link is noted in Brouk's log (`debug.log` next to `brouk.exe`), one line per link.{/win} Each line says where the link came from, which address it was routed by and which rule decided. It never contains passwords or tokens (query values are left out).
