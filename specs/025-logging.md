# 025 – Logging

- **Status:** in-progress
- **Priority:** P2
- **Depends on:** 004 (configuration), 016 (help); 019 (release builds) for logs from colleagues' copies

## Goal

When something goes wrong (a crash, a page that does not load, passwords that are not offered, Bitwarden that does not unlock), there is nothing to look at today: Brouk's messages go only to the terminal it was started from, which a normal start has none of. A log file the user can turn on, and send with a bug report, makes problems on other people's machines (phase 2) and on Windows findable without a debugger.

## Description

- Brouk always keeps a small log of warnings and errors in its data folder (`logs/brouk.log`, on Windows in `%LOCALAPPDATA%\Brouk\logs\`), rotated: at most 3 files of at most 5 MB each.
- Settings → Advanced → **Debug logging** (global, for the whole app and all profiles) adds detailed messages: what each feature decided and why (e.g. "login form found", "save offered", "ad blocker rebuilt its engine with 3 lists", "link routed to profile Work by rule …"), never the values themselves.
- It stays on across restarts and turns itself off after a set time (default 24 hours), so it is not left on by mistake.
- **Show log** opens the folder (Finder / Explorer); the help page explains what the log contains and what it never contains.
- Chromium's own log (CEF's `debug.log`) is written next to it while debug logging is on, and not at all otherwise.

**Never logged**, whatever the level: passwords, master passwords, session keys, tokens, cookies, form contents, autofill values, 2FA secrets, request and response bodies, clipboard text. URLs are logged without their query string and fragment (they can carry tokens, as a login page's `?pw=` does).

## Acceptance criteria

- [ ] Without debug logging, a warning or error from any part of Brouk is written to `logs/brouk.log` with time, level, thread and source.
- [ ] Turning debug logging on in Settings takes effect without a restart and adds the detailed messages; turning it off stops them.
- [ ] Debug logging turns itself off after the configured time, also across restarts.
- [ ] The log rotates: at most 3 files of at most 5 MB each.
- [ ] With debug logging on, Chromium's own log is written to `logs/`; with it off, it is not.
- [ ] A test signs in with a known password, saves it, fills it, unlocks a test Bitwarden vault and loads a URL with a query string, with debug logging on; the log contains none of the secret values or query strings.
- [ ] Settings has "Show log", and help documents the log (016).

## Out of scope

- Sending logs anywhere: the user attaches them to a report by hand.
- Crash reports and crash dumps (macOS keeps crash reports in DiagnosticReports; local crash dumps on Windows are not wanted, see Notes).
- Logs of web pages' consoles.

## Configuration

- `logging.debug` (bool, default off) and `logging.debug_until` (time, machine-written).
- `logging.debug_hours` (default 24).

## Open questions

None.

## Notes

- Found while chasing an intermittent crash on quit in the Windows smoke test (W3, 024): the only evidence was the exit code until CI captured Crashpad dumps.
- Implementation idea: one portable logging front (`core/log.h`) with levels, writing through a sink set by the platform; existing `LOG(WARNING)` calls keep working by setting CEF's `log_file` and `log_severity` from the setting.
- 2026-11-03 (user's decisions): warnings and errors are always logged to a file; debug logging is global, not per profile; it stays on for 24 hours by default, across restarts; Chromium's own log is written only while debug logging is on; rotation keeps at most 3 files of 5 MB; local crash dumps on Windows (Crashpad) are not needed. Spec set to `ready`.
- Verify during implementation: CEF's `debug.log` does not rotate by itself; check the CEF headers (`log_file`, `log_severity`, `log_items`) and keep it bounded too (e.g. started fresh when debug logging turns on).
- 2026-11-03 (implementation): `core/log.h` is the logging front (`LogWarning() << …`, no macros; the source file and line come from `std::source_location`), writing to stderr and to `logs/brouk.log` through `core/log_file.h` (rotation by size, 3 × 5 MB). Brouk's own `LOG(...)` and `NSLog` calls now go through it. Info and debug messages are written only while debug logging is on; the link routing line (010) moved from "always" to info. Threads are named after CEF's ("UI", "IO"…); other threads show their id.
- Chromium reads its log settings only in `CefInitialize`, so its log follows the setting from the next start: with debug logging off, `log_severity` is `DISABLE` (before, a release build wrote `debug.log` next to `brouk.exe`, or `~/Library/Logs/brouk_debug.log` on macOS, without bound). While on, it goes to `logs/chromium.log` at INFO; Chromium empties it at every start, also of a Brouk that only hands its links to the running one, so the running Brouk holds a lock on `logs/chromium.lock` (`core/file_lock.h`): a start that cannot take it writes no Chromium log, and one that can keeps the previous run's file as `chromium.1.log`. A Brouk started while the old one is still quitting (a relaunch) writes no Chromium log for that run. It is Chromium's own text and may contain full URLs; help says so.
- The end time `logging.debug_until` is a UTC time (`2026-11-04T10:30:00Z`), written when debug logging is switched on and cleared when it is off; Brouk checks it at start, after every settings change and at least hourly while on (core/debug_logging.h). Settings → Advanced holds the switch, the hours and "Show log".
- The link routing line keeps LinkForLog's form (host, start of the path, query parameter names without values), as it did before.
- Secrets test: a core test unlocks a fake Bitwarden vault and logs a URL with a query string with debug logging on and checks the log for the master password, session key, password and query values; the Windows smoke test signs in with a known password on a page whose URL has a query token, with debug logging on, and checks `brouk.log` holds neither (macOS's smoke test has no login flow and checks the rest).
