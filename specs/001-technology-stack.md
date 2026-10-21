# 001 – Technology stack

- **Status:** draft
- **Priority:** P1
- **Depends on:** none (every other spec depends on this one)

## Goal

Choose the base technology for a Chromium-based browser that is lightweight, fully configurable, runs on macOS first (Windows and mobile later), and gives us profiles, developer tools, ad blocking, a password manager and autofill with as little re-implementation as possible.

## Description

The browser must use Chromium (Blink + V8) as its rendering engine. The main decision is *how* we build on Chromium. Candidate approaches:

| Option | What it is | Pros | Cons |
|---|---|---|---|
| **A. Chromium fork** | Our own patch set on top of upstream Chromium (the approach of Brave, Vivaldi, ungoogled-chromium) | DevTools, profiles, password manager, autofill, extensions and web compatibility come built in; full control over everything | Huge codebase (C++); checkout and build need a powerful machine, lots of disk and hours per full build; patches must be rebased on every Chromium release (~every 4 weeks) |
| **B. CEF + own UI** | Chromium Embedded Framework with our own browser shell | Prebuilt binaries; small app codebase; fast builds; total UI freedom | No built-in password manager or autofill; limited extension support; adblock, password manager and autofill must be built ourselves |
| **C. Electron** | Chromium + Node.js app framework | Fastest to prototype; TypeScript | Heavier memory use, conflicts with the "lightweight" goal; webview-based browsers have a weaker security model |

Platform-native webviews (e.g. WKWebView on macOS) are excluded because they are not Chromium.

**Current leaning: Option A**, ideally starting from a lean, de-Googled base (such as ungoogled-chromium's patch set) and keeping our own patch set as small as possible. Four of the five main features (profiles, DevTools, password manager, autofill) exist in Chromium already, so the work becomes configuring, trimming and restyling instead of building from scratch.

## Acceptance criteria

- [ ] The chosen approach, language(s), UI toolkit and build system are recorded in this spec and in `CLAUDE.md`.
- [ ] A "hello world" build of the chosen base runs on macOS and opens a web page.
- [ ] The build and update process (including how we follow new Chromium releases) is documented.

## Out of scope

- Writing our own rendering or JavaScript engine.
- Mobile platforms (see 002).

## Configuration

n/a

## Open questions

- Fork (A) or CEF (B)? This is the most important decision in the project.
- What build hardware is available? A Chromium fork needs a strong Mac (lots of RAM, fast SSD, roughly 100 GB+ free disk) — cloud coding sandboxes are unlikely to be able to build it.
- Start from upstream Chromium, ungoogled-chromium, or another existing fork?
- Should Chrome extensions be supported (and which manifest version)?
- Release cadence: follow every Chromium stable release, or less often (security trade-off)?

## Notes
