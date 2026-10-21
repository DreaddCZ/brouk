# CLAUDE.md

Guidance for Claude Code when working in this repository.

## Project

A custom web browser built on Chromium via CEF (Chromium Embedded Framework). Goals:

- **Multiplatform** – macOS first, then Windows, mobile later (see `specs/002-platform-support.md`).
- **Lightweight** – low memory and CPU footprint, fast startup, no bloat.
- **Fully configurable** – behaviour, UI and shortcuts adjustable by the user.
- **Focused on work** – features that help with productive, professional use.

The user is the product owner and main tester. They do not have recent C++ experience, so code must be safe by construction and verified by tools and tests, not by the user reading it.

## Requirements workflow

All features and requirements live in [`specs/`](specs/README.md).

- `specs/README.md` is the index (every spec with its ID, title, status and priority) and contains the **roadmap** — the order in which specs are built.
- Each spec is its own file: `specs/NNN-short-name.md`, based on `specs/_template.md`.
- Statuses:
  - `idea` – rough note, not ready for work
  - `draft` – being specified, may have open questions
  - `ready` – fully specified, can be implemented
  - `in-progress` – being implemented
  - `done` – implemented and acceptance criteria verified

### Rules for Claude Code

1. **Only implement specs with status `ready`.** If asked to implement a spec that is `idea` or `draft`, point out the open questions instead of guessing.
2. **Acceptance criteria are the definition of done.** Write tests that cover them wherever practical. Criteria marked "(after NNN)" wait for that spec; they do not block the rest of the spec.
3. If a spec is ambiguous or conflicts with another spec, stop and ask rather than invent behaviour.
4. **Unspecified standard browser behaviour: do what Chrome does** (or Firefox, where Chrome has nothing comparable). If Chrome and Firefox differ and it matters, or you are unsure, ask the user. Record the choice under "Notes" in the relevant spec.
5. When starting a spec, set its status to `in-progress`; when finished, set it to `done` — in both the spec file and the index.
6. Do not change a spec's goal, description or acceptance criteria without the user's approval. Add findings under "Notes" instead.

## Decisions

- **App name: Brouk** (Czech for "beetle" — a nod to bug hunting). App bundle `Brouk.app`, executable `brouk`, bundle ID `io.github.dreaddcz.brouk` (reverse-DNS of the GitHub account, as no domain is owned; change if one is bought), data folder `~/Library/Application Support/Brouk/`. Keep these in **one place** (CMake variables `APP_NAME`, `APP_EXECUTABLE_NAME`, `APP_BUNDLE_ID`, `APP_DATA_DIR_NAME`, passed to code via a generated header); never hard-code them elsewhere.
- **Base technology: CEF** with prebuilt binaries, Chrome bootstrap and **Alloy style** browsers for web content. Not a Chromium fork. Details and reasoning: `specs/001-technology-stack.md`.
- **Profiles:** local only (no online account sign-in); tabs of several profiles can share a window, one tab group per profile per window; links are routed to profiles by URL rules. Details: `specs/005-profiles.md`, `specs/010-link-routing.md`.
- Profiles, developer tools (beyond the stock DevTools frontend), password manager and autofill are **our own implementations**, not Chrome's.
- **UI shell: hybrid** — native (Objective-C++/AppKit on macOS) for windows, tabs, toolbar, address bar, menus and the window picker; HTML/TypeScript pages in CEF for settings, history, password manager, profile manager and custom developer tools. Details: `specs/001-technology-stack.md`.
- **Languages:** C++20 for the core, Objective-C++ (AppKit) for the macOS UI, TypeScript for the HTML pages. Swift only for a macOS API not reachable from Objective-C. No Rust. Details: `specs/001-technology-stack.md`.
- **Configuration:** YAML files, global and per profile (on macOS in `~/Library/Application Support/Brouk/`), live reload, machine-written (comments not preserved). Details: `specs/004-configuration.md`.
- **Build:** CMake + Ninja, pinned CEF version downloaded by script, `tsc` for TypeScript, GoogleTest and Node's test runner for tests.
- **Phases:** phase 1 (now) — personal use, builds made locally on the user's Macs, CI runs only Linux core build and tests. Phase 2 — colleagues: signing, notarization, macOS CI, public update feed. Details: `specs/001-technology-stack.md`. "Phase 1/2" always means these release phases; platforms are rolled out separately (002).
- From phase 2, the app **updates itself automatically**; CI picks up new CEF releases, builds, signs and publishes them.

## Open decisions

- Phase 2 update feed hosting. Preferred, not final: a separate public releases repo (source repo stays private). See `specs/001-technology-stack.md`.

When a new open decision comes up, list it here until it is decided, then record it under Decisions and in the relevant spec.

## Working with CEF and the code

- **Check the CEF headers before using any CEF API.** The CEF API changes between versions and your training data may be outdated; the headers in the CEF distribution used by the build (`include/`) are the source of truth, including their doc comments. Do not rely on memory for CEF function names, signatures or behaviour.
- Same for macOS APIs that are new or recently changed: verify against the SDK headers.
- Keep quality checks strict so mistakes surface immediately: compiler warnings as errors, sanitizers (AddressSanitizer, UndefinedBehaviorSanitizer) in debug/test builds and CI, and tests for every change.
- Keep the build fast: small targets, incremental builds; avoid changes that force full rebuilds without need.
- **Keep the core portable.** Everything except platform UI must build and pass its tests on Linux. Claude Code cloud sessions run on Linux and cannot build or run the macOS app, so core work is verified there; macOS UI work is done in a session on the user's Mac.
- TypeScript pages: pin dependency versions; do not add new frameworks or libraries without the user's approval.

## Conventions

### Repository layout

```
CMakeLists.txt        top-level build; app name/bundle ID variables live here
cmake/                CMake helper modules
scripts/              fetch_cef (pinned version + checksum), build, format, lint
src/core/             portable C++: window/tab model, profiles, config, routing, storage
src/cef/              CEF integration (browser/request handlers, request contexts) — portable
src/renderer/         code running in CEF renderer processes (injected scripts, V8 bindings)
src/platform/mac/     Objective-C++ / AppKit UI and macOS integration
src/platform/win/     Windows UI (later)
web/                  TypeScript sources of internal HTML pages (settings, profile manager, dev tools…)
tests/                GoogleTest tests (mirrors src/), TypeScript tests next to web/ sources
third_party/          vendored or fetched third-party code (never edited by hand)
specs/                requirements (see above)
docs/                 developer docs: building, architecture, manual test checklists
```

Platform code is reached from the core only through interfaces defined in `src/core/` (e.g. `PlatformWindow`, `Keychain`); `src/core/` never includes platform or AppKit headers.

### C++ rules

Follow the style of CEF and Chromium, so our code and CEF read the same way.

- **Formatting:** `clang-format` with `BasedOnStyle: Chromium`, enforced in CI (`scripts/format --check`). Never argue with the formatter.
- **Naming (Chromium style):** files `snake_case.cc` / `.h` / `.mm`; types and functions `PascalCase`; variables `snake_case`; class members `snake_case_`; constants `kPascalCase`; macros `ALL_CAPS` (avoid new macros).
- **Static analysis:** `clang-tidy` in CI with the `bugprone-*`, `performance-*`, `modernize-*` and selected `cppcoreguidelines-*` checks; new code must not add warnings.
- **Compiler:** `-Wall -Wextra -Wpedantic -Wshadow -Werror` (MSVC `/W4 /WX` later). Debug and test builds use AddressSanitizer + UndefinedBehaviorSanitizer; code with threads also gets a ThreadSanitizer CI job.
- **Memory and ownership:**
  - No `new`/`delete` in our code. Use `std::make_unique` / `std::unique_ptr` for owned objects, `CefRefPtr` for CEF ref-counted objects, values wherever possible.
  - Raw pointers and references never own; prefer references when null is not valid.
  - Use `std::string_view` / `std::span` for read-only parameters; never store them beyond the call.
  - Every resource (files, handles, CF/NS objects) is owned by an RAII object. Objective-C++ files use ARC.
- **Errors:** do not throw exceptions. Return errors explicitly (`std::optional`, or the project's small `Result<T>` type). If a third-party library throws, catch at the call site and convert. Nothing may throw across a CEF callback.
- **Threads:** CEF calls us on named threads (UI, IO, renderer). Every function touching CEF or UI state states its thread in a comment and asserts it (`CEF_REQUIRE_UI_THREAD()` etc.). Cross-thread work is posted as a task (`CefPostTask`); avoid shared mutable state, and protect any that exists with a lock documented next to the data.
- **Security:** treat everything from web content and renderer processes (IPC messages, injected-script results, URLs, headers) as untrusted: validate before use. Never log passwords, tokens, cookies or form contents. Secrets go only to the OS keychain.
- **Keep it simple:** small functions and classes, one main class per `.h`/`.cc` pair, no template metaprogramming or clever tricks where plain code works. Public headers get a short comment per class and non-obvious function explaining *why*, not *what*.
- **Third-party C++ libraries:** fetched with CMake `FetchContent` at an exact version and checksum. Only well-established, maintained libraries with permissive licences (MIT, BSD, Apache-2.0, zlib, Boost); no GPL/LGPL. Each one is listed in `third_party/README.md` with version, licence and reason, and adding one is called out in the pull request. Prefer the standard library and CEF's own utilities first.

### Objective-C++ / macOS

- AppKit only (no SwiftUI). ARC enabled. UI code stays thin: it renders state from `src/core/` and forwards user actions to it; no business logic in views.
- All UI work on the main thread.
- Respect system settings: light/dark appearance, accessibility (VoiceOver labels on controls), keyboard navigation.

### TypeScript

- `tsc` with `strict: true`; no `any` (use `unknown` and narrow). Plain DOM APIs; no frameworks without approval.
- Internal pages talk to the browser only through one typed bridge module (`web/shared/bridge.ts`); the C++ side validates every message.

### Testing

- **Core (C++):** every change comes with GoogleTest tests; every bug fix adds a test that failed before the fix. Tests run on Linux in CI on every push and pull request.
- **TypeScript:** logic is unit-tested with Node's test runner.
- **UI and end-to-end:** tested manually by the user for now. Every pull request that changes behaviour includes a **manual test checklist** — short, numbered steps with the expected result, mapped to the spec's acceptance criteria — and longer-lived checklists are kept in `docs/testing/`. Automated end-to-end tests (e.g. Playwright over the Chrome DevTools Protocol, see remote debugging in 006) are planned for later; keep the code testable for that (stable accessibility labels, no hidden timing assumptions).

### Git workflow

- `main` always builds and passes tests.
- **One branch per feature or spec:** `feat/NNN-short-name` (e.g. `feat/012-web-platform-basics`), `fix/short-description` for fixes, `chore/…` for build/tooling. Spec-only or documentation-only edits may go directly to `main`.
- **Commit often:** small commits, each one building and (where practical) passing tests.
- **Commit messages follow [Conventional Commits](https://www.conventionalcommits.org/):** `type(scope): summary`
  - types: `feat`, `fix`, `refactor`, `test`, `docs`, `build`, `ci`, `chore`, `perf`, `style`
  - scope: the spec number (`feat(005): …`) or area (`build(cef): …`)
  - summary in imperative mood, lower case, no period, at most 72 characters; a body explains *why* when it is not obvious.
- When a feature (or a reviewable part of it) is done, open a **pull request** to `main` with: the spec, what changed, how it was tested, and the manual test checklist. The user tests and merges it (or asks Claude Code to merge). Merge with a merge commit, keeping the individual commits.
