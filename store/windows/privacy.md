# Brouk privacy policy

*Last updated: 2026-11-22*

Brouk is an open-source web browser (source: https://github.com/DreaddCZ/brouk). This policy covers Brouk on macOS and Windows, including Brouk from the Microsoft Store.

## In short

Brouk sends nothing to its developer. It has no accounts, no analytics, no telemetry and no advertising. Everything Brouk keeps (profiles, history, bookmarks, passwords, autofill entries, settings and logs) stays on your computer.

## What stays on your computer

- **Browsing data** (history, bookmarks, cookies, cache, site permissions, downloads list) is kept per profile in Brouk's data folder: `~/Library/Application Support/Brouk` on macOS, `%LOCALAPPDATA%\Brouk` on Windows (Brouk from the Microsoft Store: its own folder under `%LOCALAPPDATA%\Packages`).
- **Passwords** of the built-in password manager are encrypted with a key kept in the macOS Keychain or Windows Credential Manager.
- **Crash reports** are written only to your computer. Brouk never uploads them.
- **Logs** stay in the data folder. You can attach them to a bug report yourself.

## What Brouk sends, and to whom

Brouk contacts these services only for the features listed below:

| Feature | Sent to | What is sent |
|---|---|---|
| Web pages you open | The sites themselves | What any browser sends: requests, cookies of that site, your IP address |
| Search and search suggestions | The search engine you chose in Settings (Google by default) | What you type in the address bar while search suggestions are on (Settings → Search) |
| Updates (direct download only) | GitHub Pages and GitHub releases (`dreaddcz.github.io`, `github.com`) | A request for the update feed and, when there is an update, its download. No identifier. Brouk from the Microsoft Store is updated by the Microsoft Store instead. |
| Ad and tracker blocking | The publishers of the filter lists you enable (for example EasyList, uBlock Origin, AdGuard) | Requests to download the lists |
| Password checkup | Have I Been Pwned (`api.pwnedpasswords.com`) | Only the first 5 characters of each password's SHA-1 hash (k-anonymity). Never the password, and only when you run the checkup |
| Bitwarden (if you use it) | The Bitwarden server you sign in to, through the official Bitwarden CLI | What the Bitwarden CLI sends to your own vault |
| Secure DNS (if you turn it on) | The DNS provider you choose | The names of the sites you visit |

## Your choices

You can turn off search suggestions, ad blocking lists and secure DNS in Settings. **Clear browsing data** removes a profile's history, cookies and cache. Uninstalling the direct download leaves its data folder in place; delete the folder to remove everything. Uninstalling Brouk from the Microsoft Store removes its data folder.

## Children

Brouk is a general-purpose web browser. It does not knowingly collect any data from anyone, including children.

## Changes and contact

Changes to this policy are published at this address with a new date. Questions and reports: https://github.com/DreaddCZ/brouk/issues. Report security problems privately as described in https://github.com/DreaddCZ/brouk/security/policy.
