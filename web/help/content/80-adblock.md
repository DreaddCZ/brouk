## Ad blocking {#adblock}

Brouk blocks ads, trackers, malware sites and cookie banners itself, with the filter lists uBlock Origin and Brave use. Brouk's own pages, `localhost`, private network addresses and the sites in your [link routing rules](#link-routing) are never blocked unless a filter names them, so company tools and local development keep working.

### The shield {#adblock-shield}

The **shield** next to the address shows how many requests were blocked on the page. Click it for:

- an **on/off switch for this site** (in this profile);
- what was blocked, and the other sites the page contacted, each with allow and block buttons — for this site or everywhere;
- **Block element…**, **Show blocked requests** (which rule of which list blocked what — useful when a site breaks), **Report a broken site** and the settings.

A page blocked as a whole (e.g. a known malware site) shows which list blocked it, with **Allow this site** and **Go back**.

### Filter lists {#adblock-lists}

Settings → [Ad blocking](#settings-ad-blocking):

- The lists are shared by all profiles and grouped in categories: Ads, Privacy, Malware, Cookie banners and Regional (on), Domain lists and Other annoyances (off). They update by themselves; **Update now** updates them at once.
- Each profile can turn blocking off, keep its own allowlist of sites, and choose which categories it uses — e.g. no blocking on a work intranet.
- **Add a list** by its address, in any common format (Adblock Plus / uBlock syntax, hosts files, domain lists).
- **My filters** holds your own rules in the same syntax; they apply to all profiles and win over the lists. Mistakes are marked.

### Blocking an element {#adblock-picker}

Right-click on a page → **Block element…** (or use the shield). Point at the element to hide, widen the choice to its parent if needed, adjust the rule (`site##selector`) and **Preview** before saving it to My filters.

### Secure DNS {#secure-dns}

Settings → [Secure DNS](#settings-secure-dns) sends DNS lookups over HTTPS to Cloudflare, Quad9, AdGuard DNS (which also blocks ads), NextDNS, Control D or your own service, instead of your network's DNS. It is off by default. With a company VPN or intranet names, keep it off: an outside DNS service can't find internal hosts.
