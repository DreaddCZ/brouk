# 009 – Form autofill

- **Status:** in-progress
- **Priority:** P1
- **Depends on:** 001, 004, 005

## Goal

Fill repetitive forms (contact details, addresses, company details for Czech and EU invoicing) quickly and accurately, without the duplicate entries that pile up in Chrome when the same address is saved again with different spacing or a typo.

## Description

### What is stored

- **Entries:** each entry is one set of contact details with a label the user chooses (e.g. "Work", "Home", "Company A invoicing"). Any field can be empty:
  - name (first, middle, last), company, job title;
  - email, phone;
  - address: street lines, city, postal code, region, country;
  - **company identifiers:** IČO, DIČ, EU VAT ID, IBAN and BIC.
- **Custom fields:** a name and a value (e.g. "Contract number", "Cost centre"), with optional label words that identify matching form fields (e.g. "číslo smlouvy", "contract no"). They are independent of entries.
- There are **no payment cards**.
- **Shared by all profiles:** autofill data is a deliberate exception to profile isolation (005). It is not part of a profile export.

### Detecting fields

An injected script (001) recognises fields:

- first by their `autocomplete` attribute;
- then by heuristics on the field's name, id, label and placeholder, in **English and Czech** (e.g. "Jméno", "Příjmení", "Ulice", "PSČ", "IČO", "DIČ").

### Filling

- **Focusing a recognised field** shows a native drop-down with the entries that have a value for it. Each entry shows its label and a preview, e.g. "Work — Radek, Karlova 1, Praha".
- Choosing an entry fills **all recognised fields of the form** from it. Filled fields are highlighted, and the drop-down then offers "Clear filled fields".
- A custom field is suggested when the form field's label matches one of its label words.
- On any text field, the right-click menu has **Autofill →** each entry's values and each custom field, to fill one value by hand.
- Brouk fills only visible fields, in the main frame or a same-origin frame. The renderer receives only the values being filled.

### Offering to save, without duplicates

After a form is submitted with at least three recognised fields, including a name, email, phone or street, Brouk may offer to save. Before that, it compares the submitted values with the existing entries.

**Comparison** uses normalised values. The stored values keep the user's formatting. Normalising:

- trims and collapses spaces, and ignores case, accents and punctuation;
- writes phone numbers in international form; numbers without a country code use the country from the system region (e.g. `777 123 456` and `+420777123456` are equal);
- removes spaces from postal codes (`110 00` = `11000`), IČO, DIČ, VAT ID and IBAN, and makes them upper case.

**The result decides what Brouk offers:**

1. **Same** — every submitted value equals the entry's value. Nothing is offered; the entry's "last used" time is updated.
2. **Adds information** — the values match an entry, and the form adds fields the entry lacks (e.g. a phone). Brouk offers **"Add to <label>?"**, listing the new fields.
3. **Similar** — the email or the phone is the same, or name and street are nearly the same (at most 2 typing differences per field). Brouk offers **"Update <label>?"**, showing the differences, with **Update**, **Save as new** and **Not now**.
4. **Different** — Brouk offers **"Save as new entry?"** with a label field.

**Duplicates already saved:**

- The autofill settings page has **"Find duplicates"**. It groups similar entries by the same rules.
- Merging a group shows the fields side by side, and the user picks which value to keep for each field.

**Validation:**

- **Checks:**
  - IČO checksum (mod 11);
  - Czech DIČ format (`CZ` plus 8–10 digits);
  - EU VAT ID format by country;
  - IBAN checksum (mod 97).
- An invalid value is marked in the editor and in the save prompt. It is never corrected silently.

### Managing entries

Saved data is reviewed and edited in **Settings → Autofill** (`brouk://settings/autofill`, an internal page as in 004). It opens from:
- settings;
- "Manage autofill…" at the bottom of every autofill drop-down;
- the right-click Autofill menu;
- the save and update prompts ("Edit…").

**Entries list:**
- one row per entry: label, name, company, email, phone, a one-line address, and any company identifiers that are filled in;
- **last used** time and **times used**;
- a warning mark when a value fails validation;
- search across all values;
- sort by label, last used or times used (default: last used).

**Editing an entry:**
- a form with all fields, grouped as Contact, Address and Company (IČO, DIČ, VAT ID, IBAN, BIC);
- validation hints next to each field;
- an "Origin" line that says where the entry was first saved and when (e.g. "saved from eshop.example.cz, 2026-10-23"), or "added by hand".

**Custom fields list:** name, value, label words, last used. They are edited in place.

**Actions:**
- add, edit, duplicate (as a starting point for a similar entry), delete — single or several selected, with undo;
- **Find duplicates** (see above);
- **export** all autofill data to a JSON file, and **import** it, for backup and for moving to another Mac. Autofill is not in profile exports.
  - An import goes through the same duplicate checks as saving, and lists what was added, merged and skipped.

**Switches** at the top of the page:
- autofill on or off, entirely or separately for entries and custom fields;
- offer to save;
- the default country for phone numbers.

### Storage

- One SQLite database for the whole app: `autofill.db` in the app data folder.
- Values are encrypted with an app key kept in the macOS Keychain, as in 008, because entries hold personal and company data (IBAN).

## Acceptance criteria

**Entries and filling**
- [ ] Entries (contact, address, IČO, DIČ, VAT ID, IBAN, BIC) and custom fields can be added, edited and deleted; deleting can be undone.
- [ ] On a typical contact, shipping or invoicing form, in English or Czech, choosing an entry fills all recognised fields; "Clear filled fields" empties them again.
- [ ] A custom field is suggested on a form field whose label matches its label words; any value can be filled from the right-click Autofill menu.
- [ ] An entry saved while in one profile is offered in every profile.

**Saving without duplicates**
- [ ] Submitting a form whose values equal an existing entry, apart from spacing, case, accents, punctuation, phone format or postal code spacing, offers nothing.
- [ ] Submitting values that match an entry and add a field offers "Add to <label>".
- [ ] Submitting values with the same email or phone as an entry, or a name and street at most 2 typing differences away, offers "Update <label>" with the differences shown.
- [ ] Clearly different values offer "Save as new entry".
- [ ] "Find duplicates" groups similar saved entries, and merging keeps the value chosen for each field.

**Validation, settings and storage**
- [ ] An invalid IČO, DIČ, VAT ID or IBAN is marked, both in the editor and in the save prompt.
- [ ] Autofill can be turned off entirely, per kind (entries, custom fields), and offering to save can be turned off.
- [ ] Settings → Autofill can be opened from settings, from any autofill drop-down, from the right-click menu and from the save prompt.
- [ ] It lists all entries, with last used, times used and warnings, and all custom fields; it can be searched and sorted.
- [ ] Every value can be edited there; several entries can be deleted at once, with undo.
- [ ] Each entry shows where and when it was saved.
- [ ] Autofill data can be exported to a JSON file and imported again; the import skips or merges duplicates and shows a summary.
- [ ] The autofill database file alone, without the Keychain, does not reveal the stored values.

## Out of scope

- Payment cards and payment processing.
- Bitwarden identities as an autofill source.
- Import from other browsers (in line with 005); only Brouk's own JSON export can be imported.
- Looking up company details in registers (e.g. ARES by IČO) — possible later.
- Languages other than English and Czech for field detection — added when needed.

## Configuration

| Setting | Default | Scope |
|---|---|---|
| Autofill | on | global |
| Entries / custom fields | on / on | global |
| Offer to save | on | global |
| Default country for phone numbers | from the system region | global |

## Open questions

None.

## Notes

- 2026-10-21 (decision in 001): our own implementation (no Chrome autofill in CEF Alloy style); form detection via injected scripts, same storage approach as 008.
- 2026-10-21 (UI and profiles review): autofill data is shared across profiles instead of per profile.
- 2026-10-23 (autofill review with the user):
  - No payment cards.
  - Offer to save after submit, with duplicate prevention: the user has the same address several times in Chrome because of spacing and typos.
  - Built-in Czech and EU company fields plus custom fields.
  - Bitwarden is not an autofill source.
  - The normalising, comparison and validation rules are pure core functions; cover them with unit tests, including Czech examples.
  - Spec set to `ready`.
- 2026-10-23 (with the user): a place in settings to review and edit saved autofill data. Specified as Settings → Autofill, with usage stats, origin of each entry, search and sort, bulk delete and JSON export/import.
- 2026-10-25 (implementation):
  - Storage: `autofill.db`, each entry and custom field encrypted as a whole (labels included) with XChaCha20-Poly1305 and the app key in the keychain ("Brouk Keys", account `autofill-key`); only ids and usage counts are plain. Same libsodium as 008.
  - Field detection: the autocomplete attribute, then English and Czech hints in name, id, label and placeholder (`web/forms/address_rules.ts`, unit-tested); a second field of the same kind is not filled again (a second street field becomes line 2). `<select>` fields (country, region) are filled by matching an option's text or value.
  - Like Chrome: filled fields are tinted light blue until the user changes them; the drop-down lists the most recently used entries first; the prompt hangs from the right end of the address bar.
  - Offered on submit events, Enter in a field and clicks on submit buttons (not on other buttons such as "add to cart"); the same values are offered once per tab.
  - "Similar" (Update): the same email or phone, or last name and street (and first name, when both have one) at most 2 edits apart after normalising. "Adds information": all overlapping values equal and at least one new field.
  - Import of the JSON file: entries equal to a saved one are skipped, entries adding fields to one are merged into it, all others are added (similar ones can then be merged with Find duplicates).
  - The phone country is the setting `autofill.phone_country` (two letters) or the Mac's region; a national trunk 0 is dropped for countries that use one.
  - Settings → Autofill is a section of the settings page (`brouk://settings/#global:autofill` scrolls to it).
