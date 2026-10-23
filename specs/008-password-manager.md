# 008 – Password manager

- **Status:** ready
- **Priority:** P1
- **Depends on:** 001, 003, 005

## Goal

Save and fill passwords securely, either with Brouk's own password manager or with the user's company password manager. The first such integration is Bitwarden. A work profile can use the company vault while a personal profile uses the built-in manager.

## Description

### Password sources

- Each profile has one **password source**:
  - **Built-in** (default) — Brouk's own encrypted storage;
  - **Bitwarden** — the user's Bitwarden vault, through the official Bitwarden CLI (`bw`).
- The source is chosen in the profile's settings.
- Filling, saving, generating and 2FA codes look the same for both sources. Only the storage differs.
- **Why not the Bitwarden browser extension:** Brouk does not run Chrome extensions (001).
- **Other managers:**
  - 1Password could become a further source through its CLI (`op`) later.
  - Apple Passwords offers no API that browsers can use.
  - Users of any other manager copy from their own app.

### Login forms

Brouk detects login forms with an injected script in the renderer (001). It recognises:

- username, password, new password and one-time-code fields — `autocomplete` attributes first, then heuristics;
- multi-step logins (e.g. Microsoft and Google: email on one page, password on the next) — the username from step 1 is remembered for step 2.

**Matching** uses the page's exact origin (scheme, host and port) by default.

- Entries for other hosts of the same site (same registrable domain, e.g. `login.company.com` for `www.company.com`) are offered in the list, labelled with their host, but are never filled automatically, as in Chrome.
- An entry can list additional sites where it is used.
- In Bitwarden profiles, matching follows each Bitwarden item's URIs and their match settings (base domain, host, starts with, exact, regular expression, never).

**Filling:**

- With exactly one matching entry, the username and password are filled when the page loads, as in Chrome. The setting "Fill only on click" turns this off.
- Focusing a login field shows a native drop-down with the matching entries, "Generate password" (on new-password fields) and "Manage passwords…".
- A **key icon** in the address bar shows that entries exist for the site. It opens the same list, and the save prompt.

**Safety:**

- Brouk fills only visible fields, in the main frame or in a frame whose origin matches the entry.
- On `http` pages it never fills automatically, only on click, with a warning that the connection is not secure.
- The renderer never receives a password list. The browser process decides what to fill and sends only the chosen entry to that frame.

### Saving and updating

- After a login form is submitted and the login looks successful (the page navigates or the form disappears), a native prompt anchored to the key icon offers **Save**, **Never for this site** and **Not now**. The username and password can be edited in the prompt.
- If the site already has an entry with that username and a different password, the prompt offers **Update password** instead.
- In Bitwarden profiles, saving creates a Bitwarden login item. The prompt lets the user choose its folder, or an organisation and collection the user can write to. Updating changes the item's password. Brouk then syncs the vault.
- "Never for this site" is stored per profile and can be undone on the passwords page.

### Password generator

- On new-password fields, the drop-down offers a generated password: 20 characters with upper and lower case letters, digits and symbols, as configured.
- Using a generated password saves it at once, as Chrome does, so it is not lost if the sign-up fails half-way. It goes to the profile's source, built-in or Bitwarden.

### 2FA codes (TOTP)

- A built-in entry can hold a TOTP secret. It is added by pasting an `otpauth://` URI or a base32 secret, or with **"Read QR code from this page"** while the site shows its 2FA setup QR code. Brouk reads the code from its own rendering of the page; no screen recording permission is needed.
- In Bitwarden profiles, codes come from the item's TOTP field, where the user's Bitwarden plan includes TOTP.
- On a one-time-code field, the drop-down offers the current code, with a countdown. Choosing it fills the field.
- Optionally, the code is copied to the clipboard after a login is filled (off by default).

### Passkeys

- Brouk has **no passkey store of its own**. WebAuthn requests (passkeys, security keys) are handled by Chromium inside CEF.
- **Hardware security keys** (USB) work.
- **iCloud Keychain passkeys** need Apple's web-browser passkey entitlement, which Apple grants to browsers on request. It is requested in phase 2, together with the Developer ID signing.
- Passkeys stored in Bitwarden are not available, since Bitwarden offers them only through its extension.

### Passwords page

- An internal HTML page, `brouk://passwords`, opened from the key icon, settings, or Brouk → Passwords (⌥⌘P).
- It lists the built-in entries of **all profiles** that use the built-in source:
  - a **profile filter**, set to the active tab's profile when the page opens;
  - search by site and username;
  - each entry shows its profile's colour.
- A profile that uses Bitwarden appears as a single row: "Uses Bitwarden — manage in Bitwarden", with its lock state and a Lock/Unlock button.
- **Actions:** add, edit (sites, username, password, note, TOTP), delete (with undo), the "never saved" sites list, password checkup, import and export.
- **OS authentication** (Touch ID or the macOS password) is needed to reveal, copy, edit or export passwords. After one authentication, the page stays unlocked for 5 minutes while it is open.
- **Import:** CSV files from Chrome, Firefox, Safari and Bitwarden. The user chooses the target profile. Duplicates (same site, username and password) are skipped, and a summary lists what was added, skipped and conflicting.
- **Export:** a CSV in Chrome's format, for one profile. Brouk warns first that the file is not encrypted.

### Password checkup

The checkup is on the passwords page, for built-in entries. It looks for:

- **weak** passwords — estimated strength, e.g. short passwords or dictionary words;
- **reused** passwords — the same password in several entries, also across profiles (each entry is labelled with its profile);
- **breached** passwords — checked against the Have I Been Pwned password list with k-anonymity:
  - only the first 5 characters of the password's SHA-1 hash are sent;
  - the check runs when a password is saved and when the checkup is opened;
  - it can be turned off in settings.

Bitwarden entries are not part of the checkup; Bitwarden has its own reports.

### Bitwarden source

**Setup:**

- The user installs the Bitwarden CLI (Homebrew, npm or Bitwarden's download). Brouk does not bundle it. Its path is found automatically or can be set in settings.
- The user signs in once in Terminal (`bw config server …` for EU or self-hosted servers, then `bw login`, or `bw login --sso` for company SSO).
- Brouk shows these steps when it finds the CLI missing or logged out.

**Unlocking:**

- When a Bitwarden profile first needs the vault, the key icon shows a lock. Clicking it, or focusing a login field, opens Brouk's native unlock dialog for the master password.
- With **"Unlock with Touch ID"** (on by default), the session key is kept in the macOS Keychain, protected by Touch ID. After a restart, Touch ID alone unlocks the vault.
- The vault locks after 15 minutes without use (configurable), when the Mac sleeps or the screen locks, and from the passwords page.
- Several Bitwarden profiles share one unlock if they use the same Bitwarden account.

**Data handling:**

- While unlocked, Brouk holds the vault's login items **in memory only**. It loads them on unlock and syncs every 5 minutes and after each save.
- Brouk never writes Bitwarden data to disk.
- **The `bw` process:**
  - secrets are passed through environment variables or standard input, never as command-line arguments, which other processes can see;
  - Brouk runs `bw` commands directly and does **not** use `bw serve`, which would expose the unlocked vault to every local process.

### Storage (built-in source)

- Built-in passwords are stored per profile in `profiles/<id>/passwords.db` (SQLite, see 013).
- Passwords, notes and TOTP secrets are encrypted with a per-profile key. The key is kept in the macOS Keychain, readable only by Brouk's code signature, so the data files alone reveal nothing. This is the same approach Chrome uses on macOS.
- Encryption uses a well-established library (e.g. libsodium, ISC licence), added under the third-party rules in `CLAUDE.md`.
- **Profile export (005):**
  - built-in passwords are included, encrypted with a key derived from the export password (Argon2id);
  - for a Bitwarden profile, only the source setting is exported, never vault items.

## Acceptance criteria

**Saving and filling (both sources)**
- [ ] After a successful login, Brouk offers to save the credentials; "Never for this site" is respected, and changed passwords are offered as an update.
- [ ] A saved login is filled automatically on its exact origin when it is the only match; with "Fill only on click", only after choosing it in the drop-down.
- [ ] Logins for other hosts of the same site are offered but never filled automatically.
- [ ] Multi-step logins (e.g. Microsoft) fill the username on the first page and the password on the second.
- [ ] Nothing is filled into hidden fields or cross-origin frames; on http pages, filling needs a click and shows a warning.
- [ ] New-password fields offer a generated password, which is saved as soon as it is used.

**2FA, passkeys and profiles**
- [ ] A built-in entry with a TOTP secret (pasted, or read from a QR code on the page) offers the current code on one-time-code fields; a Bitwarden item with TOTP does the same.
- [ ] A USB security key can be used to sign in on a site that supports passkeys or security keys.
- [ ] Profiles never share built-in passwords; each profile uses its own source.

**Passwords page and checkup**
- [ ] The passwords page lists the built-in entries of all built-in profiles, with profile filter and search; revealing, copying, editing or exporting needs Touch ID or the macOS password.
- [ ] Deleting an entry can be undone.
- [ ] Passwords can be imported from Chrome, Firefox, Safari and Bitwarden CSV files into a chosen profile, skipping exact duplicates.
- [ ] Passwords can be exported to a Chrome-format CSV, after a warning.
- [ ] The checkup lists weak, reused (also across profiles) and breached passwords; the breach check sends only a 5-character hash prefix and can be turned off.
- [ ] The password database files alone, without the Keychain, do not reveal any password, note or TOTP secret.
- [ ] A profile export contains its built-in passwords, encrypted with the export password; it cannot be imported without it. (Completes the password criteria of 005.)

**Bitwarden source**
- [ ] A profile set to Bitwarden fills, saves and updates logins in the user's Bitwarden vault, matching by the items' URIs and match settings.
- [ ] A new login can be saved into a chosen folder or organisation collection.
- [ ] The vault unlocks with the master password in Brouk's dialog, later with Touch ID; it locks after the timeout, on sleep and on screen lock.
- [ ] No Bitwarden secret is written to disk by Brouk or passed to `bw` as a command-line argument, and `bw serve` is not used.
- [ ] If the CLI is missing or logged out, Brouk explains how to install it or sign in.

## Out of scope

- Chrome extensions, including the Bitwarden extension (001).
- 1Password and other external managers — possible later sources.
- Apple Passwords and iCloud Keychain passwords.
- Brouk's own passkey store, and Bitwarden passkeys.
- Sharing passwords with other people, and sync of built-in passwords between devices (Bitwarden syncs itself).
- Bitwarden's identities, cards and secure notes, and Bitwarden vault health reports.
- Automatic password changes on sites.

## Configuration

| Setting | Default | Scope |
|---|---|---|
| Password source | built-in (alternative: Bitwarden) | per profile |
| Offer to save passwords | on | per profile |
| Fill only on click | off | global |
| Generated password | 20 characters; upper, lower, digits, symbols | global |
| Copy 2FA code after filling a login | off | global |
| Check saved passwords for breaches | on | global |
| Bitwarden CLI path | found automatically | global |
| Bitwarden: lock after | 15 minutes (also on sleep and screen lock) | global |
| Bitwarden: unlock with Touch ID | on | global |

## Open questions

None.

## Notes

- 2026-10-21 (decision in 001): with CEF Alloy style there is no Chrome password manager, so this is our own implementation: injected scripts detect login forms, secrets go to the OS keychain, save/fill prompts are our own UI.
- 2026-10-23 (passwords review with the user):
  - The user's company uses Bitwarden through its browser extension, which Brouk cannot run. The agreed model is a password source per profile: built-in, or Bitwarden through the official CLI, with 1Password possible later.
  - Passkeys: system ones only (security keys now, iCloud Keychain after the entitlement in phase 2).
  - TOTP from both sources.
  - Checkup: weak, reused and breached (HIBP with k-anonymity).
  - The passwords page shows all built-in profiles with a filter, like history and bookmarks.
  - Spec set to `ready`.
- 2026-10-23: as of Bitwarden client 2026.1, Bitwarden does not register as a macOS AutoFill (credential) provider, so that route is not available. If it ships, reconsider whether Brouk can use it.
- Check before implementing:
  - the current `bw` commands and output (`status`, `unlock --passwordenv`, `list items`, `get totp`, `create item`, `edit item`, `sync`, `lock`);
  - CEF's WebAuthn support on macOS (security keys; whether hybrid sign-in with a phone works);
  - the Keychain access-control API for Touch ID protection.
- The QR code reader needs a library (e.g. zxing-cpp, Apache-2.0), and the strength estimate may use `@zxcvbn-ts/core` (MIT). Both need the user's approval and a listing under the third-party rules.
