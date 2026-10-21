# 009 – Form autofill

- **Status:** draft
- **Priority:** P1
- **Depends on:** 001, 005, 008

## Goal

Fill repetitive forms (addresses, contact details, payment info) quickly and accurately.

## Description

- Store addresses and contact details (name, company, email, phone, address).
- Store payment cards (encrypted; CVV never stored).
- Detect form fields and suggest matching entries; fill the whole form with one choice.
- Multiple entries per type (e.g. work vs. personal address).
- Manage entries on a settings page. Autofill data is shared by all profiles — a deliberate exception to profile isolation (see 005); passwords stay per profile (008).

## Acceptance criteria

- [ ] User can add, edit and delete address/contact entries and payment cards.
- [ ] On a typical checkout or contact form, selecting a suggestion fills all matching fields.
- [ ] Card numbers are encrypted at rest; showing or filling a card requires OS authentication.
- [ ] Autofill data is shared by all profiles: an entry saved in one profile is offered in every profile.
- [ ] Autofill can be disabled entirely or per data type.

## Out of scope

- Payment processing or wallet services.

## Configuration

Enable/disable addresses, enable/disable payment cards, require OS auth for cards.

## Open questions

- Should payment cards be supported at all, or addresses/contacts only?
- Offer to save new addresses/cards automatically when a form is submitted?
- Custom autofill fields for work (e.g. VAT number, company ID / IČO)?
- Import from other browsers?

## Notes

- 2026-10-21 (decision in 001): our own implementation (no Chrome autofill in CEF Alloy style); form detection via injected scripts, same storage approach as 008.
- 2026-10-21 (UI and profiles review): autofill data is shared across profiles instead of per profile.
