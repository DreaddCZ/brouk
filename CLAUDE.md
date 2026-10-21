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
- Profiles, developer tools (beyond the stock DevTools frontend), password manager and autofill are **our own implementations**, not Chrome's.
- The app **updates itself automatically**; CI picks up new CEF releases, builds, signs and publishes them.

## Open decisions

These are not settled yet — do not assume an answer:

- UI shell: native per platform (Swift/AppKit) vs. HTML/TypeScript UI in CEF vs. hybrid.
- Implementation language(s).
- Build system and CI setup.
- Configuration format and where settings are stored.

Once decided, record them here and in a dedicated spec.

## Conventions

To be defined once the tech stack is chosen (code style, testing, commit messages, branching).
