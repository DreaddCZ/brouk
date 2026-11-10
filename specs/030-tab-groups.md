# 030 – Tab groups within a profile

- **Status:** draft
- **Priority:** P2
- **Depends on:** 003, 005, 015, 026; shapes 031

## Goal

A profile group with many tabs is hard to work in: tabs for different tasks (a ticket, a test run, documentation) mix in one row. Groups inside a profile group let the user keep each task's tabs together, collapse what they are not working on, and close or move a whole task at once.

## Description

Rough draft: options and questions to be decided later.

- Inside a profile group, the user can put tabs into **tab groups** (Chrome's tab groups), each with a name and a colour, collapsible. A tab is in at most one tab group; tabs outside any tab group stay as today.
- A tab group always belongs to one profile: it lives inside that profile's group and cannot hold tabs of another profile.
- Commands as in Chrome: Add Tab to Group (new group / existing group), Remove from Group, and for the group: rename, colour, New Tab in Group, Ungroup, Close Group, Move Group to New Window.
- Dragging (026): tabs into and out of a tab group; a whole tab group within its profile group and to other windows of the same profile group.
- Session restore (015) brings tab groups back with their name, colour and collapsed state.

### Options

- **A. Chrome-style groups only.** Groups exist while they have tabs, per window. Simple; matches what users know.
- **B. Saved groups** (Chrome's "Save group"): a group can be closed and reopened later with all its tabs, from a menu or the command palette. Useful for recurring tasks (e.g. "Release testing").
- **C. Workspaces:** a named set of groups per window that replaces the visible tabs (Arc / Edge workspaces). Most powerful, but a different model; 003 says workspaces are not needed for now.

Proposal: A first, B as a later step if wanted; C out of scope.

## Acceptance criteria

To be written once the open questions are answered.

## Out of scope

- Tab groups that mix profiles (profiles stay isolated, 005).
- Workspaces (option C), unless decided otherwise.
- Syncing groups between devices.

## Configuration

To be decided; candidates: colour of new groups (next free / profile colour), whether collapsing a group keeps its tabs loaded.

## Open questions

1. Which option: A, B or both? Should saved groups (B) be part of the profile export (005)?
2. How are tab groups drawn in the horizontal strip next to profile groups, without confusing the two? Ideas: profile group label stays the large coloured label, tab groups get a smaller label with a coloured underline (Chrome); or tab groups are shown only with vertical tabs (031).
3. Colours: Chrome's nine colours are already used for profiles. Separate set, the same set, or tab groups take shades of their profile's colour?
4. Tab overflow (003): when space is short, which collapses first — other profile groups, or tab groups in the active profile group?
5. Nesting: only one level (profile → tab group), or tab groups inside tab groups? Proposal: one level.
6. Can a tab group be moved to another profile (reopening all its tabs, with the 005 confirmation), or is that not allowed?
7. Does the command palette find tab groups, and can a shortcut switch between groups?
8. Collapsed groups: unload their tabs to save memory (lightweight goal), or keep them loaded as Chrome does?
9. Are tab groups also the unit for link routing (010), e.g. "open ticket links in group X", or only profiles?

## Notes

- 2026-11-10: created as a rough draft at the user's request, together with 031. 005 lists "several groups of the same profile in one window" as out of scope; that remains true — these are groups inside the one profile group, not a second profile group.
