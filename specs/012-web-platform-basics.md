# 012 – Web platform basics

- **Status:** in-progress
- **Priority:** P1
- **Depends on:** 001; later integrates with 003, 004, 005

## Goal

Web sites work and behave as they do in Chrome. CEF renders pages, but in Alloy style many standard browser functions are left to the application (dialogs, permission prompts, authentication, errors). Without them, common sites — video calls, sign-ins, file uploads, internal company tools — break or behave oddly.

## Description

**Rule for this spec: copy Chrome's behaviour** (Firefox where Chrome has nothing comparable). Where the two differ in a way that matters, or the right behaviour is unclear, Claude Code asks the user during development and records the answer under Notes. Visual design follows our own UI (003); behaviour follows Chrome.

Functions covered:

1. **JavaScript dialogs** — `alert`, `confirm`, `prompt` and the "leave site?" dialog (`beforeunload`), shown tab-modal (they block only their tab), with Chrome's protection against dialog spam (option to suppress further dialogs from the page).
2. **File chooser** — native open/save dialogs for `<input type="file">` (single, multiple, folder upload) and the File System Access API where CEF supports it; drag and drop of files onto pages.
3. **Permission prompts** — camera, microphone, screen sharing (window/screen picker), notifications, geolocation, clipboard read, MIDI, and others CEF reports. Ask / allow / block, remembered **per site and per profile**, revocable from the address bar's site info and from settings. macOS system permissions (camera, mic, screen recording, location) are requested when first needed, with a hint if the user denied them in System Settings.
4. **Web notifications** — shown as native macOS notifications; clicking one focuses its tab.
5. **Authentication** — HTTP basic/digest/NTLM/Negotiate login dialog; **client certificate selection** from the macOS keychain when a site asks for one (common in company networks).
6. **Certificate and security errors** — Chrome-style interstitial page for invalid certificates, with "proceed anyway" for non-HSTS sites; lock/warning state in the address bar; site info popup with connection and certificate details.
7. **Error pages** — page not found, no connection, DNS failure, timeouts; **crashed tab** page with reload ("Aw, Snap" equivalent); **unresponsive page** dialog (wait / close).
8. **Find in page** — `Cmd+F`, next/previous, match count, highlight all.
9. **Zoom** — `Cmd +`, `Cmd -`, `Cmd 0`, pinch; remembered per site per profile, as in Chrome; indicator in the address bar when not 100 %.
10. **Printing** — print dialog and save as PDF.
11. **Full screen** — HTML full-screen API (videos, presentations) with Chrome's "press Esc to exit" hint; browser full-screen via the macOS full-screen mode.
12. **Context menu** — Chrome's standard items for pages, links, images, selected text and editable fields: back/forward/reload, open link in new tab, copy link, save image, copy image, copy, paste, spell-check suggestions, "Inspect" (see 006).
13. **Spell checking** — in editable fields, using the macOS system spell checker and its languages, as Chrome on macOS does.
14. **External protocols** — links such as `mailto:`, `tel:`, `msteams:`, `zoommtg:`, `slack:` ask before opening the external app, with "always allow for this site", as in Chrome.
15. **Popups and new windows** — Chrome's popup blocker (popups without a user gesture are blocked, with an address-bar indicator to allow them); `window.open` popups with features open as a popup window, others as a new tab.
16. **PDF viewer** — PDFs open in the tab with CEF's built-in PDF viewer.
17. **Media** — audio/video playback including the codecs available in CEF's standard distribution; Chrome's autoplay policy; tab audio indicator with mute; Picture-in-Picture.
18. **Downloads (interim)** — until the downloads spec (014) exists, downloads behave like Chrome's default: files are saved to `~/Downloads` without asking, with a unique name if the file exists, and a minimal native notice when finished (click → show in Finder). "Save link as" / "Save image as" ask where to save.
19. **Basic keyboard and editing behaviour** — standard macOS text editing shortcuts, undo/redo, select all, and Chrome's page shortcuts (space/shift-space scroll, `Cmd+[`/`Cmd+]` back/forward, `Cmd+L` address bar, `Cmd+R` reload, `Cmd+W` close tab, `Cmd+T` new tab).

## Acceptance criteria

- [ ] `alert`, `confirm`, `prompt` and `beforeunload` dialogs appear tab-modal and return the user's choice to the page.
- [ ] File inputs open a native dialog; multiple files and folder upload work; files can be dropped onto pages.
- [ ] A video call in Microsoft Teams, Google Meet and Zoom (web) works in the browser: camera, microphone and screen sharing prompts appear, and the choices are remembered per site and profile.
- [ ] A permission granted to a site in profile A is not granted to the same site in profile B.
- [ ] Permissions can be reviewed and revoked from the address bar and from settings.
- [ ] Web notifications appear as macOS notifications; clicking one opens its tab.
- [ ] A site using HTTP basic authentication shows a login dialog, and the login works.
- [ ] A site requiring a client certificate offers the matching certificates from the keychain.
- [ ] An expired or self-signed certificate (e.g. `expired.badssl.com`, `self-signed.badssl.com`) shows a warning page; the user can proceed on non-HSTS sites only.
- [ ] Network errors show an error page with a retry button; a crashed renderer shows a crash page with reload; a hung page offers to wait or close.
- [ ] Find in page works with match count and next/previous.
- [ ] Zoom works by keyboard and pinch and is remembered per site per profile.
- [ ] Printing and "Save as PDF" work.
- [ ] HTML full-screen works for video, with an exit hint; the browser window can enter macOS full screen.
- [ ] Context menus offer Chrome's standard items for pages, links, images, selections and text fields.
- [ ] Spelling mistakes in text fields are underlined, with suggestions in the context menu.
- [ ] `mailto:` and other external protocol links ask before opening the external app.
- [ ] Popups without a user gesture are blocked with an indicator; allowed popups open.
- [ ] PDFs display in the tab.
- [ ] Audio and video play (e.g. YouTube); a playing tab shows an audio indicator and can be muted.
- [ ] Downloads are saved to `~/Downloads` with a notice when finished.
- [ ] Common sites render and work as in Chrome — at least: Google (Search, Gmail, Docs), Microsoft 365 (Outlook, Teams, SharePoint), Azure Portal, GitHub, Atlassian (Jira, Confluence), YouTube. Differences are recorded as bugs.

## Out of scope

- Full downloads manager (014), history (013), bookmarks (011).
- Password saving prompts (008) and autofill (009).
- Chrome extensions (decided in 001: not supported).
- Widevine DRM (Netflix and similar) — not available in CEF's standard distribution; may be revisited if needed.
- Proprietary codecs beyond what CEF's standard distribution includes (some H.264/AAC content may not play) — revisit if a work site needs them.

## Configuration

- Per profile, per site: permissions, zoom level, popups allowed, external protocol choices (all managed by the browser, editable in settings).
- Global: default zoom (100 %), download folder (`~/Downloads`), ask where to save each download (off), spell-check on/off.

## Open questions

None. Behaviour not described here follows Chrome; ask the user when unsure.

## Notes

- 2026-10-21 (decided with the user): permission prompts copy Chrome's current prompt — a panel under the address bar, "<site> wants to" plus one line per permission; camera, microphone and location offer "Allow while visiting the site" / "Allow this time" / "Never allow"; other permissions "Allow" / "Block"; screen sharing is asked every time and never remembered. Decisions are stored in Chromium's content settings of the tab's request context, i.e. per site and per profile. "Allow this time" ends when the tab leaves the site or closes.
- 2026-10-21 (implementation): network error pages, certificate interstitials (with HSTS handling) and the PDF viewer come from Chromium itself through CEF's Chrome bootstrap. CEF reports no "tab is playing audio" event (its audio handler captures audio instead of playing it), so the audio indicator needs another approach.
- 2026-10-22 (implementation): through CEF's Chrome bootstrap, Chromium itself provides the HTTP sign-in dialog (Chrome's), the certificate interstitial, network error pages and the PDF viewer; CEF does not call our handlers for these.
- 2026-10-22 (implementation): CEF reports neither popups blocked by Chromium's popup blocker nor whether a tab plays audio. A small script added to every page reports both to the browser (renderer process code in `src/renderer/`); the reports are treated as untrusted hints that only update indicators. The audio indicator is an estimate (playing media with sound, or a running Web Audio context), not Chrome's measured audio output.
- 2026-10-22 (decided with the user): screen sharing uses a picker. CEF's media path alone shares the whole main screen, so the page script replaces `getDisplayMedia()`: Brouk shows **macOS's own screen sharing picker** (ScreenCaptureKit, the same picker other Mac apps use; Chrome has its own, with the same choice of a window or a screen), and the script then requests exactly the chosen source. Choosing in the picker is the permission: the capture that follows is allowed once, for that site, within 10 s, without a second question. Like Chrome, it needs a click (otherwise `InvalidStateError`); cancelling gives `NotAllowedError`. System audio is offered when a whole screen is shared (macOS asks once for "Record your system audio"). Limits: Chrome's "share a tab" is not offered; on macOS before 15.2 the picker offers only screens and the main screen is shared (older macOS does not report the choice); CEF does not tell which source a capture request names, so a page could, within those 10 s, capture a different window than the one chosen.
- 2026-10-22 (implementation, open): web notifications are granted and macOS has authorised Brouk to show notifications, but none appeared in the first tests; still being investigated (Chromium's macOS notification path or a Focus mode).
- 2026-10-22 (implementation): a tab opened only to start a download stays open (Chrome closes it). In CEF a download continues after its tab closes, but without further callbacks, so the download notice would lose track of it.
- 2026-10-21: spec created from the readiness review. The user's instruction: copy Chrome or Firefox behaviour, check what users prefer when unsure, or ask during development.
- Roadmap: built in milestone 1 ("browse the web") together with 001 and a minimal shell (see roadmap in `specs/README.md`). Until profiles exist (005), everything is stored for one implicit default profile; storage keys are already per profile so nothing needs migrating.
- CEF hooks to check in the headers before implementing: `CefJSDialogHandler`, `CefDialogHandler`, `CefPermissionHandler`, `CefRequestHandler` (auth, certificate errors, client certificates, render process termination), `CefFindHandler`, `CefPrintHandler`, `CefDisplayHandler` (full screen, title, favicon), `CefContextMenuHandler`, `CefDownloadHandler`, `CefLifeSpanHandler` (popups). Some functions may have default implementations in CEF on macOS; use them where they match Chrome's behaviour and our UI.
- Codecs: CEF's standard binary distribution is built without proprietary codecs; check what plays during milestone 1 testing (Teams/Meet video should use VP8/VP9/AV1, which are included).
