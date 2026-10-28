# 023 – Open-source licences (credits)

- **Status:** draft
- **Priority:** P1
- **Depends on:** 016 (help), 019 (release pipeline)

## Goal

Brouk ships third-party software: Chromium and CEF, yaml-cpp, SQLite, libsodium, adblock-rust and its Rust crates, Brave's adblock-resources, Sparkle, and the password strength estimate (zxcvbn-ts). Their licences (BSD, MIT, ISC, Apache-2.0, MPL-2.0 and Chromium's many) require the copyright notices and licence texts to go along with every copy of the app. The published builds do not have them yet. Users should also be able to see what Brouk is built on, as Chrome shows in `chrome://credits`.

## Description

- **`brouk://credits`**, an internal page that opens in a tab of the current profile, as `chrome://credits` does in Chrome.
  - It starts with Brouk's own licence: Apache-2.0, with the copyright line from `NOTICE`.
  - It then lists every third-party component in the app, one section each, with:
    - the name, version and licence,
    - a link to the project's page,
    - the full licence text, folded until clicked.
  - Rust crates and npm packages are listed one by one, because each has its own copyright notice.
  - A search box filters the list.
- **`brouk://credits/chromium`** shows the Chromium and CEF credits page from the CEF distribution (`CREDITS.html`), unchanged. Brouk embeds CEF's binaries as they are, so that page lists what they contain.
- **Opening it:**
  - **About Brouk** gets an "Open-source licences" link.
  - The **Help menu** gets "Open-Source Licences".
  - A short help section (016) explains the page.
- **Generated at build time.** A build script collects the licence texts:
  - from the fetched sources (FetchContent, `third_party/`);
  - from Cargo's metadata and crate sources, for the Rust crates actually linked;
  - from `node_modules`, for the npm packages copied into pages.

  It writes one data file that the page reads.
- **The build fails** if a shipped component has no licence text. Adding a third-party component to the app (`third_party/README.md`) therefore also means making sure its licence reaches this page.
- **Only what ships** is listed. Build tools, test libraries (GoogleTest) and build-time packages (TypeScript, markdown-it) are not.

## Acceptance criteria

- [ ] `brouk://credits` opens in a tab. It shows Brouk's Apache-2.0 licence and copyright first, then one section per shipped component, each with name, version, licence and full licence text.
- [ ] The components listed: CEF, yaml-cpp, SQLite (its public-domain dedication), libsodium, adblock-rust, every Rust crate linked into the app (from `Cargo.lock`, without build-only crates), Brave's adblock-resources, Sparkle (macOS), and the zxcvbn-ts packages with their dependencies. The versions match `third_party/README.md` and the lock files.
- [ ] `brouk://credits/chromium` shows the CEF distribution's `CREDITS.html`.
- [ ] About Brouk and the Help menu open `brouk://credits`, and help has a section on it.
- [ ] The search box filters the components by name and licence.
- [ ] A test fails when a shipped component has no licence text, or when `third_party/README.md` lists a component the page does not.
- [ ] Works offline: everything is inside the app, nothing is downloaded.
- [ ] The release builds (019) contain the page. The release smoke test checks that it loads.

## Out of scope

- Licence texts for the filter lists that are downloaded at runtime (007). They are not part of the app, and the ad blocking page already names each list's publisher.
- A separate licences file in the disk image or on the releases page. The page inside the app covers every copy.
- Windows (002), until the Windows app exists. The generator is portable.

## Configuration

None.

## Open questions

None.

## Notes

- 2026-10-28 (first version): needed before or with the next release. Published builds 0.1.0–0.2.0 shipped without these notices. Chrome's equivalent is `chrome://credits`, opened from Settings → About Chrome; Firefox has `about:license`.
