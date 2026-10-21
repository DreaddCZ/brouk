# CLAUDE.md

Guidance for Claude Code when working in this repository.

## Project

A custom web browser built on Chromium via CEF (Chromium Embedded Framework). Goals:

- **Multiplatform** – macOS first, then Windows, mobile later (see `specs/002-platform-support.md`).
- **Lightweight** – low memory and CPU footprint, fast startup, no bloat.
- **Fully configurable** – behaviour, UI and shortcuts adjustable by the user.
- **Focused on work** – features that help with productive, professional use.

## Requirements workflow

All features and requirements live in [`specs/`](specs/README.md).

- `specs/README.md` is the index: every spec with its ID, title, status and priority.
- Each spec is its own file: `specs/NNN-short-name.md`, based on `specs/_template.md`.
- Statuses:
  - `idea` – rough note, not ready for work
  - `draft` – being specified, may have open questions
  - `ready` – fully specified, can be implemented
  - `in-progress` – being implemented
  - `done` – implemented and acceptance criteria verified

### Rules for Claude Code

1. **Only implement specs with status `ready`.** If asked to implement a spec that is `idea` or `draft`, point out the open questions instead of guessing.
2. **Acceptance criteria are the definition of done.** Write tests that cover them wherever practical.
3. If a spec is ambiguous or conflicts with another spec, stop and ask rather than invent behaviour.
4. When starting a spec, set its status to `in-progress`; when finished, set it to `done` — in both the spec file and the index.
5. Do not change a spec's goal, description or acceptance criteria without the user's approval. Add findings under "Notes" instead.

## Decisions

- **Base technology: CEF** with prebuilt binaries, Chrome bootstrap and **Alloy style** browsers for web content. Not a Chromium fork. Details and reasoning: `specs/001-technology-stack.md`.
- **Profiles:** local only (no online account sign-in); tabs of several profiles can share a window, one tab group per profile per window; links are routed to profiles by URL rules. Details: `specs/005-profiles.md`, `specs/010-link-routing.md`.
- Profiles, developer tools (beyond the stock DevTools frontend), password manager and autofill are **our own implementations**, not Chrome's.
- **UI shell: hybrid** — native (Objective-C++/AppKit on macOS) for windows, tabs, toolbar, address bar, menus and the window picker; HTML/TypeScript pages in CEF for settings, history, password manager, profile manager and custom developer tools. Details: `specs/001-technology-stack.md`.
- **Languages:** C++20 for the core, Objective-C++ (AppKit) for the macOS UI, TypeScript for the HTML pages. Swift only for a macOS API not reachable from Objective-C. No Rust. Details: `specs/001-technology-stack.md`.
- **Configuration:** YAML files, global and per profile (on macOS in `~/Library/Application Support/<app>/`), live reload, machine-written (comments not preserved). Details: `specs/004-configuration.md`.
- **Build:** CMake + Ninja, pinned CEF version downloaded by script, `tsc` for TypeScript, GoogleTest and Node's test runner for tests.
- **Phases:** phase 1 (now) — personal use, builds made locally on the user's Macs, CI runs only Linux core build and tests. Phase 2 — colleagues: signing, notarization, macOS CI, public update feed. Details: `specs/001-technology-stack.md`.
- From phase 2, the app **updates itself automatically**; CI picks up new CEF releases, builds, signs and publishes them.

## Open decisions

- Phase 2 update feed hosting. Preferred, not final: a separate public releases repo (source repo stays private). See `specs/001-technology-stack.md`. When a new one comes up, list it here until it is decided, then record it under Decisions and in the relevant spec.

## Working with CEF and the code

- **Check the CEF headers before using any CEF API.** The CEF API changes between versions and your training data may be outdated; the headers in the CEF distribution used by the build (`include/`) are the source of truth, including their doc comments. Do not rely on memory for CEF function names, signatures or behaviour.
- Same for macOS APIs that are new or recently changed: verify against the SDK headers.
- Keep quality checks strict so mistakes surface immediately: compiler warnings as errors, sanitizers (AddressSanitizer, UndefinedBehaviorSanitizer) in debug/test builds and CI, and tests for every change.
- Keep the build fast: small targets, incremental builds; avoid changes that force full rebuilds without need.
- **Keep the core portable.** Everything except platform UI must build and pass its tests on Linux. Claude Code cloud sessions run on Linux and cannot build or run the macOS app, so core work is verified there; macOS UI work is done in a session on the user's Mac.
- TypeScript pages: pin dependency versions; do not add new frameworks or libraries without the user's approval.

## Conventions

To be defined once the tech stack is chosen (code style, testing, commit messages, branching).
