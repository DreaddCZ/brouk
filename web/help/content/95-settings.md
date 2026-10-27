## Settings {#settings}

**Brouk → Settings…** (<kbd>⌘,</kbd>) — or find any setting with the [command palette](#command-palette). Choose a topic on the left. Settings shared by all profiles come first; below them, under **For each profile**, a tab for each profile. Changes apply at once.

### Settings files {#settings-files}

Every setting is also in a plain text file (YAML), which you can edit while Brouk runs — Brouk applies the change within a second:

- `~/Library/Application Support/Brouk/settings.yaml` — settings for all profiles;
- `~/Library/Application Support/Brouk/profiles/<id>/settings.yaml` — one profile's settings;
- `link-routing.yaml` (rules and wrappers) next to `settings.yaml`, and `request-rules.yaml` in each profile folder.

Brouk rewrites the files when you change settings in the app, so comments are not kept. A value Brouk can't use is ignored and listed at the top of the settings page, and a yellow warning in the address bar leads there. Settings → Settings files → **Show in Finder** opens the folder; **Export Settings…** and **Import Settings…** move the settings for all profiles as one file ([profiles](#profiles-export) have their own export).

### Settings reference {#settings-reference}

Every setting on the settings page, with its key in the files, default and scope:

<div class="generated" data-generated="settings"><p class="muted">Loading…</p></div>
