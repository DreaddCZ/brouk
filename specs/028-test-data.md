# 028 – Test data generator

- **Status:** idea
- **Priority:** P3
- **Depends on:** 027 (utilities for testers)

## Goal

Testers fill forms with made-up data again and again. Valid-looking, varied test data at hand saves time and finds validation bugs.

## Description

To be specified. Ideas from 2026-11-04:

- Random strings of a given length and character set (also boundary cases: empty, very long, Unicode, emoji, right-to-left text, injection-like strings such as `<script>` or `' OR 1=1`).
- Lorem ipsum text of a length.
- Names, emails, phone numbers, addresses, dates.
- Czech identifiers that pass validation: IČO, DIČ, rodné číslo, bank account numbers and IBAN; other countries' VAT IDs.
- Filling a form on the page with generated data (relation to autofill, 009).
- Repeatable data from a seed; export as JSON or CSV.

## Acceptance criteria

To be written.

## Out of scope

To be decided.

## Configuration

To be decided.

## Open questions

- Which kinds of data, and which countries, for the first version?
- Part of the Utilities (027) or a tool of its own? Fill forms on the page directly?

## Notes

- 2026-11-04: split from 027 at the user's request ("random test data and Lorem ipsum could be another small feature, I can imagine a lot of different possibilities for data generation").
