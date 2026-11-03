# 025 – Logging: manual test checklist

Run on macOS and on Windows. The log folder is `~/Library/Application Support/Brouk/logs/` (Windows: `%LOCALAPPDATA%\Brouk\logs\`).

1. Start Brouk with debug logging off (the default). Expected: `logs/brouk.log` exists; there is no `chromium.log` in `logs/`, and on Windows no `debug.log` next to `brouk.exe` (on macOS none in `~/Library/Logs/`).
2. Cause a warning, e.g. type `theme: purple` under `appearance:` in `settings.yaml`. Expected: a `WARNING` line with time, thread and source file appears in `brouk.log`; no `DEBUG` lines.
3. Settings → Advanced → turn **Debug logging** on. Expected: at once "Logging: debug logging switched on" in the log; `settings.yaml` gets `logging.debug_until` 24 hours from now (UTC). Open a page with a login form: `DEBUG` lines like "Passwords: 1 login form(s) on https://…".
4. Sign in on a test login page with a known password and save it; sign in again so it is filled; open a link from another app; open an address with `?token=…`. Expected: the log tells what happened (form found, save offered, filled, link routed by rule …) but never the password, the user name or the query value.
5. With a Bitwarden profile, unlock the vault. Expected: "Bitwarden: unlocked with the master password", "read N login(s)"; never the master password.
6. Restart Brouk. Expected: debug logging is still on, and `logs/chromium.log` is now written.
7. Set `logging.debug_until` in `settings.yaml` to a time a minute from now (UTC, e.g. `2026-11-04T10:30:00Z`). Expected: within about a minute after it, "debug logging switched off" in the log, and `logging.debug` is `false` in the file. The same happens at the next start if Brouk was not running at that time.
8. Turn debug logging off in Settings. Expected: "switched off" is logged and no more `DEBUG` lines follow.
9. Settings → Advanced → **Show log**. Expected: Finder (File Explorer) opens with `brouk.log` selected. The "?" next to it opens help at "Log and debug logging".
10. Rotation (optional): with debug logging on, browse a lot or make `brouk.log` large; Expected: never more than `brouk.log`, `brouk.1.log` and `brouk.2.log`, each at most 5 MB.
