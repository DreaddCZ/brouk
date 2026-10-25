# Manual test checklist: form autofill (009)

Serve the test pages (`docs/testing/pages/009/README.md`):

    cd docs/testing/pages/009 && python3 -m http.server 8766

## Entries and filling

1. Settings (⌘,) → scroll to **Autofill: saved entries** → **Add entry**: label
   "Work", name, email, phone `777 123 456`, street, city, postal code
   `110 00`, IČO `25596641`, DIČ `CZ25596641`, IBAN
   `CZ65 0800 0000 1920 0014 5399`. → Listed with name, contact, address and
   identifiers, "Last used never · used 0× · added by hand".
2. Open `http://localhost:8766/checkout.html` (Czech) and click into
   **Jméno**. → A drop-down shows "Work" with a preview and "Manage
   autofill…". Choose Work. → All recognised fields fill (name, email,
   phone, street, city, PSČ, country, company, IČO, DIČ) and are tinted
   blue.
3. Click into a filled field again. → The drop-down also offers **Clear
   filled fields**; it empties exactly the filled fields.
4. Same on `shipping.html` (English, autocomplete attributes): full name,
   address lines, ZIP, country, phone, email, VAT number, IBAN fill.
5. Add a custom field: name "Contract number", value `SML-2026-77`, label
   words `číslo smlouvy`. On checkout.html click **Číslo smlouvy**. → The
   drop-down offers the custom field; choosing it fills that field only.
6. Right-click any text field (e.g. Poznámka) → **Autofill** → Work → a value,
   or the custom field. → That one value is inserted. **Manage autofill…**
   opens Settings at the Autofill section.
7. Switch to another profile's tab and repeat step 2. → The same entries are
   offered (autofill is shared by all profiles).

## Saving without duplicates

8. On checkout.html type the Work values with different formatting (`radek`,
   `Sevcik` without accents, `+420777123456`, `Karlova  1,`, `11000`) and
   submit. → Nothing is offered; the entry's "used" count goes up.
9. Submit the Work values plus a value it does not have (e.g. a company). →
   "Add to “Work”?" lists the new field; **Add** stores it.
10. Submit with the same email but another street. → "Update “Work”?" shows
    the difference (old → new) with **Update**, **Save as new** and **Not
    now**.
11. Submit with the name and street one or two typos away (e.g. `Radke`,
    `Karlova 11`) and another email. → Also "Update “Work”?".
12. Submit a completely different person (e.g. Eva Nováková, Vodičkova 30,
    IČO `12345678`). → "Save as new entry?" with a label field; the invalid
    IČO is marked in orange and saved as typed (never corrected). **Edit…**
    opens Settings → Autofill.
13. Turn off **Offer to save what you type into forms** (Settings →
    Autofill). → Step 12 offers nothing. Turn off **Offer saved entries** /
    **Offer custom fields** / **Fill forms…**: the drop-down loses entries,
    custom fields, or everything.

## Settings → Autofill

14. The list shows last used, times used, a ⚠︎ mark on entries with invalid
    values, and each entry's origin ("saved from localhost, 2026-…" for
    step 12). Search (accents and case ignored) and sort (last used, label,
    times used) work.
15. **Edit…** an entry: all fields grouped Contact / Address / Company, the
    validation hint under an invalid value, the origin line. **Duplicate**
    starts a new entry from a copy.
16. Select two entries with the check boxes → **Delete 2 selected** → **Undo**
    in the notice brings both back. Deleting a custom field can be undone
    too.
17. Create two similar entries (same email, slightly different street). →
    **Find duplicates** lists them; **Merge…** shows the differing fields
    side by side; the chosen values are kept in one entry.
18. **Export…** saves a JSON file (warned: not encrypted). Delete an entry,
    then **Import…** the file. → "1 added, … merged, … already saved".
    Importing again adds nothing.

## Storage

19. `~/Library/Application Support/Brouk/autofill.db` in a text viewer or
    `sqlite3` shows no names, addresses or identifiers (all encrypted with
    the key in the keychain, "Brouk Keys").
