# 006 – Developer tools

- **Status:** draft
- **Priority:** P1
- **Depends on:** 001

## Goal

Give web developers the full developer tooling they know from Chrome and Firefox.

## Description

Provide Chromium DevTools: elements inspector, console, sources/debugger, network, performance, memory, application/storage, device emulation and Lighthouse-style audits where available. Opened with standard shortcuts (`Cmd+Opt+I` on macOS, `F12`/`Ctrl+Shift+I` on Windows) and from the context menu ("Inspect").

## Acceptance criteria

- [ ] DevTools open via shortcut, menu and right-click "Inspect".
- [ ] DevTools can be docked (bottom, side) or opened in a separate window.
- [ ] Elements, Console, Sources, Network, Performance, Memory and Application panels work.
- [ ] Device/responsive emulation works.
- [ ] Remote debugging can be enabled via a setting or command-line flag.

## Out of scope

- Building custom DevTools panels (possible later).

## Configuration

Dock position, remote debugging port (off by default).

## Open questions

- Should DevTools be hidden by default for non-developers (a "developer mode" toggle)?
- Any extra developer features beyond Chromium DevTools (e.g. quick viewport switcher, cookie/header editor in toolbar)?

## Notes
