/**
 * Which form fields want autofill values (009): names, company, contact,
 * address and Czech/EU company identifiers. Like Chrome: the autocomplete
 * attribute first, then hints in the field's name, id, label and
 * placeholder, in English and Czech. Pure functions over FieldInfo (see
 * field_rules.ts); login.ts reads the page.
 */
import type { FieldInfo } from './field_rules.js';

/** The kinds as the browser names them (core/autofill_entry.h). */
export type AutofillKind =
  | 'first_name' | 'middle_name' | 'last_name' | 'full_name' | 'company' | 'job_title' | 'email' | 'phone'
  | 'street' | 'street_line1' | 'street_line2' | 'city' | 'postal_code' | 'region' | 'country'
  | 'ico' | 'dic' | 'vat_id' | 'iban' | 'bic';

const AUTOCOMPLETE: Readonly<Record<string, AutofillKind>> = {
  'given-name': 'first_name',
  'additional-name': 'middle_name',
  'family-name': 'last_name',
  name: 'full_name',
  organization: 'company',
  'organization-title': 'job_title',
  email: 'email',
  tel: 'phone',
  'tel-national': 'phone',
  'street-address': 'street',
  'address-line1': 'street_line1',
  'address-line2': 'street_line2',
  'address-level2': 'city',
  'postal-code': 'postal_code',
  'address-level1': 'region',
  country: 'country',
  'country-name': 'country',
};

/** Hints in order: the first that matches wins (more specific first). */
const HINTS: readonly (readonly [RegExp, AutofillKind | null])[] = [
  // Login names are not autofill fields.
  [/uživatelsk|přihlašovací|login|username|user.?name/u, null],
  [/\biban\b|číslo účtu \(iban\)/, 'iban'],
  [/\b(bic|swift)\b/, 'bic'],
  [/i[cč]\.? ?dph|\bvat\b|ust-?id|vat.?(id|number)|tax.?id/, 'vat_id'],
  [/(?<!\p{L})d\.? ?i\.? ?[cč](?!\p{L})|daňové identifika/u, 'dic'],
  [/(?<!\p{L})i\.? ?[cč]\.? ?o?(?!\p{L})|identifikační číslo|company.?(id|number|reg)|reg(istration)?.?(no|number)/u, 'ico'],
  [/e-?mail/, 'email'],
  [/phone|tel(efon)?\b|mobil|\btel\./, 'phone'],
  [/first.?name|given.?name|fname|křestní|(?<!\p{L})jm[eé]no(?!\p{L})(?!.*příjmení)/u, 'first_name'],
  [/last.?name|sur.?name|family.?name|lname|příjmení|prijmeni/, 'last_name'],
  [/middle.?name/, 'middle_name'],
  [/full.?name|your.?name|jméno a příjmení|celé jméno|^\s*name\s*$/u, 'full_name'],
  [/company|organi[sz]ation|firma|firmy|společnost|spolecnost|název firmy/, 'company'],
  [/job.?title|position|pozice|funkce/, 'job_title'],
  [/address.?(line)?.?2|addr2|č\.? ?p\.? ?2|doplňující/, 'street_line2'],
  [/street|address|addr|ulice|adresa|č\.? ?p\./, 'street'],
  [/zip|postal|post.?code|psč|psc/, 'postal_code'],
  [/city|town|město|mesto|obec/, 'city'],
  [/region|state|province|county|kraj/, 'region'],
  [/country|země|zeme|stát\b|stat\b/, 'country'],
];

const ADDRESS_TEXT_TYPES = new Set(['text', 'email', 'tel', '', 'search', 'number']);

function addressHintText(field: FieldInfo): string {
  return `${field.name} ${field.id} ${field.label} ${field.placeholder}`.toLowerCase().replace(/[_]+/g, ' ');
}

/** The autofill kind of one field, or null. */
export function autofillKind(field: FieldInfo, isSelect = false): AutofillKind | null {
  if (!field.visible || (!isSelect && !ADDRESS_TEXT_TYPES.has(field.type))) {
    return null;
  }
  const tokens = field.autocomplete.split(/\s+/).filter((t) => t !== '');
  for (const token of tokens) {
    const kind = AUTOCOMPLETE[token];
    if (kind !== undefined) {
      return kind;
    }
  }
  if (field.type === 'email') return 'email';
  if (field.type === 'tel') return 'phone';
  const text = addressHintText(field);
  if (/search|hledat|captcha|coupon|kupón|promo|password|heslo/.test(text)) {
    return null;
  }
  for (const [pattern, kind] of HINTS) {
    if (pattern.test(text)) {
      return kind;
    }
  }
  return null;
}

/**
 * The kinds of a form's fields. A kind already used by an earlier field is
 * not given again (a second "street" becomes street line 2), so one value
 * is not filled twice.
 */
export function classifyAddressForm(fields: readonly FieldInfo[], selects: readonly boolean[] = []): (AutofillKind | null)[] {
  const used = new Set<AutofillKind>();
  return fields.map((field, i) => {
    let kind = autofillKind(field, selects[i] ?? false);
    if (kind === null) return null;
    if (used.has(kind)) {
      if (kind === 'street' || kind === 'street_line1') {
        kind = 'street_line2';
      } else if (kind !== 'email') {
        return null;
      }
    }
    if (kind === 'street' && used.has('street_line1')) kind = 'street_line2';
    used.add(kind);
    return kind;
  });
}
