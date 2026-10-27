# 018 – Feedback and issue reporting

- **Status:** draft
- **Priority:** P2
- **Depends on:** 001 (phase 2), 004, 016

## Goal

Make it easy for beta testers (colleagues, from phase 2) to report problems and ask for features **from inside Brouk**, without a GitHub account and without having to collect version numbers or logs themselves. Reports land as GitHub issues, next to the specs, so they feed straight into development. Most testers work on client systems, so a report must never carry client data unless the tester chooses to add it and has seen it.

The feature works the same on **macOS and Windows** (002). Only the menu location, shortcuts, file locations and the mail app differ.

## Description

### Opening the form

- **Send Feedback…** opens the internal page `brouk://feedback` in its own window, like help (016). Opening it again brings the same window to the front.
  - **macOS:** Help menu → "Send Feedback…".
  - **Windows:** the app menu → Help → "Send Feedback…", like Chrome's menu (where exactly follows the Windows UI, 002).
  - Both platforms: the command palette (003), and a shortcut, `shortcuts.feedback`, remappable (003). The default is ⌥⇧⌘I on macOS and Alt+Shift+I on Windows, as Chrome's "Report an issue".
- The help page (016) and the crashed-tab page (012) have a "Send feedback" link. From the crashed tab, the form opens as a bug about that tab.

### The form

- **Type:** Bug or Feature idea (a switch at the top; the fields below change with it).
- **Title** (required, up to 120 characters).
- **Bug:** "What happened?" (required), "What did you expect?", "Steps to reproduce" (optional), "How often?" (once / sometimes / every time).
- **Feature idea:** "What would you like?" (required), "What would it help you with?" (optional).
- **Area** (optional): a short list matching the specs, e.g. Tabs and windows, Profiles, Link routing, Developer tools, Passwords, Autofill, Bookmarks, History, Downloads, Ad blocking, Other.
- **Your name and email** (optional), so the developer can ask follow-up questions. Asked once, remembered in the global settings, and changeable in the form.
- The form keeps a **draft** while it is being filled in, so closing the window or quitting Brouk does not lose the text. A draft is kept until it is sent or discarded.

### Diagnostics

Always included, and shown in a "What will be sent" section of the form before sending:

- Brouk version and build channel, CEF and Chromium version;
- operating system and version (for example "macOS 15.4" or "Windows 11 24H2, build 26100"), and CPU architecture (Apple Silicon, Intel, x64, ARM64);
- display count and scale factors, system language, light or dark appearance;
- number of profiles, windows and tabs (counts only, no names);
- which optional features are switched on, for example ad blocking, dev tools in the current tab, password source type, VPN route type (017) — no names, hosts or values;
- a random **installation ID**, created on first use and stored in the global settings, used only to group reports and limit their rate. It can be reset in settings.

**Optional attachments, off by default.** The tester ticks each one and sees it before sending:

- **Current page address** of the tab the form was opened from — shown in full and editable, so a client host can be shortened or removed.
- **Screenshot** of that tab, from Brouk's own rendering (no screen recording permission is needed on either platform). The tester can **black out areas** before sending by dragging rectangles over them.
- **Recent log**: Brouk's log of the last 10 minutes, with secrets removed (see Privacy). Shown as text before sending.

### Sending

- **Send** posts the report over HTTPS to the **feedback relay** (below). On success the form shows "Thank you — report #<number> was created" and closes the draft.
- If sending fails (offline, relay down, rate limit), the report stays as a draft with the reason, and two choices:
  - **Try again** — also retried automatically the next time Brouk starts with a network connection, once;
  - **Send by email** — opens the system's default mail app (Mail on macOS, Outlook or another default on Windows) with a new message to the feedback address, the report text and diagnostics in the body. Attachments that do not fit in a `mailto:` link (screenshot, log) are saved to a file the tester attaches by hand; the form says where it is and offers "Show in Finder" / "Show in Explorer".
- Brouk never sends anything in the background except a report the tester sent or chose to retry.

### Feedback relay

- A tiny web service that receives reports from Brouk and creates GitHub issues. It is needed because a GitHub token cannot be put into the app: anyone could take it out of the app bundle or the `.exe`.
- The relay:
  - holds the GitHub credentials (a GitHub App installation token, limited to issues of one repository);
  - checks every report (required fields, sizes, allowed values) and rejects anything else;
  - creates an issue with the title, a body built from the fields and diagnostics, and labels `from-beta`, `bug` or `feature`, the area, and the platform (`macos` / `windows`);
  - uploads an attached screenshot and log so they are linked from the issue, and not readable by anyone outside the repository;
  - limits reports per installation ID and per IP address (for example 10 per hour) and refuses reports over 5 MB;
  - returns the issue number.
- The relay's code lives in this repository (`relay/`), in TypeScript, with its own tests. It does not depend on which platform the report came from.
- The address of the relay is a setting (advanced), so a company can run its own.

### Privacy

- Nothing is sent without the tester pressing **Send**, and everything sent is shown first.
- Never included unless the tester attaches it on purpose: page addresses, page content, profile names, window or tab titles, bookmarks, history, passwords, form contents, cookies, tokens, VPN connection names or servers.
- The log is cleaned before it is shown: values that look like tokens (JWTs, `Bearer …`, API keys), cookies, email addresses other than the tester's own, and URL query strings are replaced by `<removed>`. This uses the same list of secret-looking names as the API export in 006.
- The relay keeps no copy of reports after creating the issue, and logs only what it needs for rate limiting.

## Acceptance criteria

- [ ] On macOS, Help → Send Feedback… and ⌥⇧⌘I open `brouk://feedback` in its own window; opening it again reuses that window.
- [ ] On Windows, app menu → Help → Send Feedback… and Alt+Shift+I open the same form. (after Windows support, 002)
- [ ] The shortcut can be remapped, and the form can be opened from the command palette.
- [ ] Choosing Bug or Feature idea shows the matching fields; the form cannot be sent without the required fields.
- [ ] A draft survives closing the window and restarting Brouk, until it is sent or discarded.
- [ ] Diagnostics (version, OS and version, architecture, counts, features switched on, installation ID) are shown in the form before sending, and the report contains exactly what is shown.
- [ ] The page address, screenshot and log are off by default; each is attached only when ticked and is shown before sending; the address can be edited and areas of the screenshot can be blacked out.
- [ ] The log shown and sent has tokens, cookies, other people's email addresses and URL query strings replaced by `<removed>`.
- [ ] A sent report creates a GitHub issue with the fields, diagnostics, labels for type, area and platform, and the attachments; the form shows the issue number.
- [ ] When the relay cannot be reached, the report stays as a draft, Try again works, and Send by email opens the default mail app with the report; attachments are saved to a file shown in Finder (macOS) or Explorer (Windows).
- [ ] No feedback request is made unless the tester pressed Send or Try again (or accepted the one automatic retry of a failed report).
- [ ] The relay rejects invalid reports, reports over 5 MB and reports over the rate limit, with an error the form shows.
- [ ] The app contains no GitHub token or other relay secret.
- [ ] The installation ID can be reset in settings.
- [ ] The crashed-tab page (012) and the help page (016) link to the form; from a crashed tab, the form opens as a bug.
- [ ] Help describes how to send feedback and what is sent. (after 016)

## Out of scope

- **Automatic crash reports** (CEF's Crashpad uploading crash dumps) — a possible later spec, which could reuse the relay.
- A **"My reports"** page showing testers the status of what they sent — later.
- Voting on feature ideas, or discussion inside Brouk; testers can be invited to the repository (or a separate feedback repository) for that.
- Feedback from mobile (not supported yet, 002).
- Usage statistics or any telemetry.

## Configuration

Global (`settings.yaml`):

- `feedback.name`, `feedback.email` — remembered tester details, empty by default.
- `feedback.installation_id` — created on first use; "Reset" in settings.
- `feedback.relay_url` (advanced) — default: Brouk's relay.
- `feedback.email_address` (advanced) — where "Send by email" sends to.
- `shortcuts.feedback` — ⌥⇧⌘I on macOS, Alt+Shift+I on Windows.

Drafts and the attachments saved for email are kept in the data folder (`feedback/`): on macOS `~/Library/Application Support/Brouk/feedback/`, on Windows in Brouk's data folder there (location decided with Windows support, 004), readable by the user only.

## Open questions

1. **Relay hosting:** Cloudflare Workers (free tier, preferred) or another service. Decide together with the update feed hosting at the start of phase 2 (001, "Update process", point 4).
2. **Target repository:** issues go to the source repository (testers who are invited see code) or to a separate `brouk-feedback` repository (testers can be invited to follow their reports without seeing code)?
3. **Feedback email address** for the "Send by email" fallback.
4. **Who can send:** anyone with a build, or only testers who were given an invitation code (protects the relay if builds become public)?

## Notes

- 2026-10-25 (proposal, from discussion with the user): built-in form → relay → GitHub issues, so testers need no GitHub account; must work for future Windows users as well as macOS. Planned for phase 2, together with signed builds and the update feed.
- 2026-10-27 (user's decision, interim): until this form exists, beta testers report through GitHub issues in the public releases repository `DreaddCZ/brouk-releases`, with two issue forms (Bug report, Feature idea; blank issues off) that warn the repository is public and ask for no client data. Recommended for this spec (open question 2 still to be decided): reports from the form go to a **private** repository, and issues in `brouk-releases` are switched off once the form works.
- Implementation hints:
  - The form is an internal HTML page (`web/feedback/`) and talks to the browser through `web/shared/bridge.ts`; diagnostics, the screenshot and the cleaned log are gathered by the browser process (C++), not by the page, and the browser process sends the report, so the page never gets network access of its own.
  - Platform-specific parts (OS version, architecture, opening the mail app, revealing a file, log location) go through interfaces in `src/core/` with macOS and Windows implementations; the report builder and the log cleaner are portable core code, tested on Linux.
  - Screenshot: the tab's last rendered frame, as for window thumbnails in 010.
  - The log cleaner gets its own tests with real-looking samples (JWTs, Azure and Atlassian URLs, cookies) so nothing slips through.
