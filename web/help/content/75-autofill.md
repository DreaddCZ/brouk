## Autofill {#autofill}

Autofill fills contact, address and company details into forms. It is **shared by all profiles**, kept encrypted on your Mac, and has no payment cards.

- **Entries:** one set of details each, with a label you choose ("Work", "Home", "Company A invoicing"): name, company, job title, email, phone, address, and company identifiers (IČO, DIČ, EU VAT ID, IBAN, BIC).
- **Custom fields:** a name and value (e.g. "Contract number") with words that identify matching form fields.
- Fields are recognised in English and Czech forms.

**Filling:** click a recognised field and choose an entry: all recognised fields of the form are filled and highlighted, and the list then offers **Clear filled fields**. Right-click any text field → **Autofill** to fill a single value.

**Saving without duplicates:** after you submit a form, Brouk compares it with your entries, ignoring formatting (spaces, case, accents, `+420` or not, `110 00` = `11000`):

- the same details → nothing is asked;
- the form adds something (e.g. a phone) → **Add to <entry>?**;
- the same email or phone, or a nearly identical name and street → **Update <entry>?** with the differences, or **Save as new**;
- something new → **Save as new entry?**

IČO, DIČ, VAT ID and IBAN are checked; an invalid value is marked, never corrected silently.

**Settings → [Autofill](#settings-autofill)** lists the entries and custom fields with search, edits them, **finds duplicates** and merges them field by field, and exports or imports them as JSON (**not encrypted**). Phone numbers without a country code are read as numbers of the country set there (by default your Mac's region).
