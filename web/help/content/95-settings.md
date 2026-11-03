## Settings {#settings}

**Brouk → Settings…** (<kbd>⌘,</kbd>) — or find any setting with the [command palette](#command-palette). Choose a topic on the left. Settings shared by all profiles come first; below them, under **For each profile**, a tab for each profile. Changes apply at once.

### Settings files {#settings-files}

Every setting is also in a plain text file (YAML), which you can edit while Brouk runs — Brouk applies the change within a second:

- `~/Library/Application Support/Brouk/settings.yaml` — settings for all profiles;
- `~/Library/Application Support/Brouk/profiles/<id>/settings.yaml` — one profile's settings;
- `link-routing.yaml` (rules and wrappers) next to `settings.yaml`, and `request-rules.yaml` in each profile folder.

Brouk rewrites the files when you change settings in the app, so comments are not kept. A value Brouk can't use is ignored and listed at the top of the settings page, and a yellow warning in the address bar leads there. Settings → Settings files → **Show in Finder** opens the folder; **Export Settings…** and **Import Settings…** move the settings for all profiles as one file ([profiles](#profiles-export) have their own export).

### Log and debug logging {#logging}

When something goes wrong, Brouk's log helps find out why. Attach it to your bug report; Brouk never sends it anywhere by itself.

- **Where:** `~/Library/Application Support/Brouk/logs/brouk.log` (on Windows `%LOCALAPPDATA%\Brouk\logs\brouk.log`). Settings → Advanced → **Show log** shows it in Finder or File Explorer. It is kept small: at most 3 files of 5 MB, the oldest is deleted.
- **Always:** warnings and errors, with time, thread and the place in Brouk's code.
- **Debug logging** (Settings → Advanced) adds what each feature decided and why — a login form found, a password offered, a link routed by a rule, the ad blocker's lists loaded. It applies at once and switches itself off after 24 hours (change it with "Turn debug logging off after"), also across restarts. While it is on, Chromium's own log is written next to it as `chromium.log`, from the next start of Brouk; that file is Chromium's, not Brouk's, and can contain full addresses, so look through it before you send it.
- **Never in Brouk's log** (`brouk.log`): passwords, master passwords, keys, tokens, cookies, form contents, autofill values, two-factor codes, request and response bodies or clipboard text. Addresses appear without the part after `?` or `#` and without user names, as those can carry tokens.

### Settings reference {#settings-reference}

Every setting on the settings page, with its key in the files, default and scope:

<div class="generated" data-generated="settings"><p class="muted">Loading…</p></div>
