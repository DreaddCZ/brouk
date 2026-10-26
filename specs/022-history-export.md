# 022 – History in profile export

- **Status:** ready
- **Priority:** P3
- **Depends on:** 005, 013

## Goal

Take a profile's history along when moving the profile to another device, so address bar suggestions and the history page work there from the start.

## Description

Moved here from 005 and 013 (user's decision, 2026-10-26), so history (013) could be finished without it.

- When exporting a profile (005), the user chooses whether to include its history.
- If included, the user limits it to either:
  - the last **N days**, or
  - the last **N visited pages**.
- The history goes into the profile export file as a new section. The file stays at version 1, as 005 planned for added sections. Older versions list the section as one they cannot import.
- On import, the history becomes the imported profile's history: its pages, visits, titles and how each visit started.
- The retention limit (013) still applies after the import, so visits older than the limit are removed.
- Not exported: the downloads list (014), the Clean up keep list, favicons.

## Acceptance criteria

- [ ] When exporting a profile, the user can choose to include history, limited by days or by number of pages; by default it is not included.
- [ ] Importing such a file restores those visits into the imported profile's history; they are suggested in the address bar and listed on the history page under that profile.
- [ ] A file exported without history imports as before; a file with history imports in a version without this spec, with the section listed as not imported.

## Out of scope

- Exporting history on its own, outside a profile export.
- Merging history into an existing profile.
- The downloads list, favicons and the Clean up keep list.

## Configuration

None; the choice is made in the export dialog.

## Open questions

None.

## Notes

- 2026-10-26 (user's decision): split off from 005 and 013, where it had been postponed since 2026-10-23, so 013 can be marked done.
