# 007 – Ad and tracker blocking

- **Status:** draft
- **Priority:** P1
- **Depends on:** 001, 004, 005

## Goal

Block ads, trackers and annoyances natively, without needing an extension, keeping pages fast and private.

## Description

A built-in content blocker that:

- Uses standard filter list formats (Adblock Plus / uBlock Origin syntax) — network blocking plus cosmetic (element hiding) filters.
- Ships with sensible default lists (e.g. EasyList, EasyPrivacy) that update automatically.
- Lets the user add custom lists and custom rules.
- Can be turned off per site (allowlist), e.g. for internal work tools.
- Shows the number of blocked items for the current page.

A possible engine is Brave's open-source `adblock-rust`, which is designed for this purpose; to be decided in the detailed spec.

## Acceptance criteria

- [ ] Ads and trackers on common test pages are blocked with default lists enabled.
- [ ] Cosmetic filters hide ad placeholders.
- [ ] Filter lists update automatically on a configurable schedule.
- [ ] User can add/remove filter lists by URL and write custom rules.
- [ ] User can disable blocking for a site with one click; the choice persists.
- [ ] Blocking adds no noticeable page-load overhead.

## Out of scope

- Blocking YouTube ads specifically (arms race; may be handled by filter lists only).

## Configuration

Enabled lists, custom rules, per-site allowlist, update frequency, blocking on/off per profile.

## Open questions

- Engine: adblock-rust, an existing Chromium-fork implementation, or something else?
- Per-profile or global filter settings?
- Also block cookie banners and other annoyances by default?

## Notes

- 2026-10-21 (decision in 001): Chrome extensions are not supported, so this built-in blocker is the only ad blocking; it should cover what uBlock Origin does (network and cosmetic filtering, uBO filter syntax). Engine choice: `adblock-rust` conflicts with the "no Rust" language decision unless used as a prebuilt library through its C interface (adds a Rust toolchain to the build); the alternative is our own C++ engine. Decide when this spec is detailed.
