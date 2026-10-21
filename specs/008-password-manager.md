# 008 – Password manager

- **Status:** draft
- **Priority:** P1
- **Depends on:** 001, 005

## Goal

Securely save and fill passwords so the user does not need a separate password manager for everyday browsing.

## Description

- Offer to save credentials after login; update them when changed.
- Fill saved credentials on login forms (automatically or on click).
- Generate strong passwords on sign-up/change-password forms.
- A passwords page to view, search, edit, delete and export entries — protected by OS authentication (Touch ID / system password on macOS, Windows Hello later).
- Passwords are stored encrypted using the OS keychain (macOS Keychain, Windows DPAPI).
- Separate password storage per profile.

## Acceptance criteria

- [ ] After logging in, the browser offers to save the credentials; "never for this site" is respected.
- [ ] Saved credentials are filled on the matching site (exact origin match by default).
- [ ] Password generator produces strong passwords on new-password fields.
- [ ] Viewing a saved password requires OS authentication.
- [ ] Passwords are encrypted at rest; data files alone cannot reveal them.
- [ ] Passwords can be imported from and exported to CSV (Chrome/Firefox format).
- [ ] Profiles do not share passwords.

## Out of scope

- Sync between devices (later spec).
- Sharing passwords with other people.

## Configuration

Offer to save (on/off), auto-fill vs. fill on click, generator rules, require OS auth to view.

## Open questions

- Integrate with external managers (1Password, Bitwarden, Apple Passwords) instead of or in addition to the built-in one?
- Passkeys support?
- TOTP (2FA codes) support?
- Breach / weak / reused password warnings?

## Notes
