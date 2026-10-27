## Passwords {#passwords}

Brouk has its own password manager. Each profile keeps its own passwords, encrypted on your Mac; the key is in the macOS keychain. A profile can use your [Bitwarden](#passwords-bitwarden) vault instead.

### Filling and saving {#passwords-filling}

- When a site has exactly one saved login, Brouk fills it as the page loads. Settings → [Passwords](#settings-passwords) → **Fill only on click** turns that off.
- Clicking a login field shows a list of the site's logins, **Generate password** on sign-up forms, and **Manage passwords…**. Use <kbd>↑</kbd> <kbd>↓</kbd> and <kbd>Return</kbd> to choose.
- The **key icon** in the address bar shows that the site has saved logins and opens the same list.
- After you sign in, Brouk offers **Save** (or **Update password**), **Never for this site** or **Not now**. You can correct the username and password before saving.
- Logins of other hosts of the same site (e.g. `login.example.com` for `www.example.com`) are listed with their host but never filled automatically.
- On pages without `https`, Brouk fills only when you click, and warns that the connection is not secure.

### Generated passwords {#passwords-generator}

On a sign-up or change-password field, choose **Generate password**: 20 characters with upper and lower case letters, digits and symbols (Settings → Passwords sets the length and characters). The password is saved at once, so it is not lost if the sign-up fails half-way.

### 2FA codes {#passwords-2fa}

A saved login can hold a 2FA (TOTP) secret. Add it on the passwords page by pasting the `otpauth://` link or the secret, or with **Read QR code from this page** in the key icon's menu while the site shows its 2FA setup QR code. On a code field, the list then offers the current code with a countdown. Settings → Passwords can copy the code to the clipboard after a login is filled.

### Passwords page {#passwords-page}

**Brouk → Passwords** (<kbd>⌥⌘P</kbd>) lists the saved logins of all profiles, with a profile filter and search. Add, edit (sites, username, password, note, 2FA), delete with undo, and manage the "never saved" sites.

Showing, copying, editing or exporting passwords asks for **Touch ID** or your Mac's password; the page then stays unlocked for 5 minutes while it is open.

### Password checkup {#passwords-checkup}

The checkup on the passwords page finds **weak**, **reused** (also across profiles) and **breached** passwords. The breach check uses Have I Been Pwned: only the first 5 characters of the password's SHA-1 hash are sent, never the password. Turn it off in Settings → Passwords.

### Bitwarden {#passwords-bitwarden}

Settings → [Passwords](#settings-passwords) → **Passwords** → **Bitwarden** makes a profile fill and save logins from your Bitwarden vault (bitwarden.com or your company's own Bitwarden or Vaultwarden server) instead of the built-in manager.

**Setup, once:**

1. Install the Bitwarden CLI, e.g. `brew install bitwarden-cli` in Terminal. Brouk finds it in Homebrew's folders and your `PATH`; Settings → Passwords → **Bitwarden CLI** sets another path.
2. Sign in once in Terminal with `bw login` (for a self-hosted or EU server first `bw config server https://…`). If the server uses its own certificates, Brouk shows the exact command to use — it starts with `NODE_EXTRA_CA_CERTS=…` — when you click the lock icon. Brouk prepares that certificate file itself; nothing to export.

**Using it:**

- While the vault is locked, the key icon shows a **lock**, and login fields offer **Unlock Bitwarden…**. Enter your master password; Brouk shows progress while `bw` unlocks, and a wrong password is shown in the same dialog.
- With **Unlock Bitwarden with Touch ID** on (the default), later unlocks — also after a restart — ask only for Touch ID.
- The vault locks after 15 minutes without use (**Lock Bitwarden after**), when the Mac sleeps or the screen locks, and with **Lock Bitwarden** on the passwords page.
- Logins match pages as in Bitwarden, following each item's URI match setting (by default, every host of the site).
- New logins are saved to Bitwarden; the save prompt's **Save in** chooses the folder or an organisation's collection. Updated passwords change the item in Bitwarden.
- Items with **Master password re-prompt** are never filled automatically; choosing one asks for Touch ID (or the master password when Touch ID is off).
- Edit, delete and check Bitwarden logins in Bitwarden itself; the passwords page lists only the built-in manager's logins.

Your master password and the vault's logins are never written to disk; the logins are kept in memory only while the vault is unlocked.

### Import and export {#passwords-import-export}

- **Import** CSV files from Chrome, Firefox, Safari or Bitwarden into a profile of your choice. Duplicates are skipped and a summary lists what was added.
- **Export** a profile's logins as a CSV in Chrome's format. **The CSV is not encrypted** — delete it when you no longer need it.
- A [profile export](#profiles-export) includes the profile's passwords, encrypted with an export password.

If you build Brouk yourself, macOS may ask once after an update for access to Brouk's key in the keychain; allow it.
