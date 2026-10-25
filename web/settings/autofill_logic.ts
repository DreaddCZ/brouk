/** Settings → Autofill (009) as pure functions, tested without a DOM. */
import type { AutofillEntryRow } from './autofill_api.js';

export interface FieldDef {
  readonly name: string;
  readonly label: string;
  readonly multiline?: boolean;
}

/** The editor's groups (009: Contact, Address and Company). */
export const kFieldGroups: readonly { readonly title: string; readonly fields: readonly FieldDef[] }[] = [
  {
    title: 'Contact',
    fields: [
      { name: 'first_name', label: 'First name' },
      { name: 'middle_name', label: 'Middle name' },
      { name: 'last_name', label: 'Last name' },
      { name: 'company', label: 'Company' },
      { name: 'job_title', label: 'Job title' },
      { name: 'email', label: 'Email' },
      { name: 'phone', label: 'Phone' },
    ],
  },
  {
    title: 'Address',
    fields: [
      { name: 'street', label: 'Street', multiline: true },
      { name: 'city', label: 'City' },
      { name: 'postal_code', label: 'Postal code' },
      { name: 'region', label: 'Region' },
      { name: 'country', label: 'Country' },
    ],
  },
  {
    title: 'Company',
    fields: [
      { name: 'ico', label: 'IČO' },
      { name: 'dic', label: 'DIČ' },
      { name: 'vat_id', label: 'EU VAT ID' },
      { name: 'iban', label: 'IBAN' },
      { name: 'bic', label: 'BIC' },
    ],
  },
];

export const kAllFields: readonly FieldDef[] = kFieldGroups.flatMap((g) => g.fields);

export type EntrySort = 'last_used' | 'label' | 'times_used';

export function fullName(entry: AutofillEntryRow): string {
  return ['first_name', 'middle_name', 'last_name'].map((f) => entry.values[f] ?? '').filter((v) => v !== '').join(' ');
}

/** "Karlova 1, 110 00 Praha" on one line. */
export function addressLine(entry: AutofillEntryRow): string {
  const street = (entry.values['street'] ?? '').replace(/\n/g, ', ');
  const town = [entry.values['postal_code'] ?? '', entry.values['city'] ?? ''].filter((v) => v !== '').join(' ');
  return [street, town, entry.values['country'] ?? ''].filter((v) => v !== '').join(', ');
}

/** Entries containing every word of the query in any value or the label. */
export function searchEntries(entries: readonly AutofillEntryRow[], query: string): AutofillEntryRow[] {
  const fold = (s: string): string => s.normalize('NFD').replace(/\p{M}/gu, '').toLowerCase();
  const words = fold(query).split(/\s+/).filter((w) => w !== '');
  return entries.filter((e) => {
    const hay = fold([e.label, ...Object.values(e.values)].join(' '));
    return words.every((w) => hay.includes(w));
  });
}

export function sortEntries(entries: readonly AutofillEntryRow[], by: EntrySort): AutofillEntryRow[] {
  const copy = entries.slice();
  copy.sort((a, b) => {
    if (by === 'label') return a.label.localeCompare(b.label) || a.id - b.id;
    if (by === 'times_used') return b.timesUsed - a.timesUsed || b.lastUsed - a.lastUsed;
    return b.lastUsed - a.lastUsed || b.created - a.created;
  });
  return copy;
}

/** "saved from eshop.example.cz, 2026-10-23" or "added by hand". */
export function originLine(entry: AutofillEntryRow): string {
  if (entry.originSite === '') return 'added by hand';
  const date = entry.created > 0 ? new Date(entry.created * 1000).toISOString().slice(0, 10) : '';
  return date === '' ? `saved from ${entry.originSite}` : `saved from ${entry.originSite}, ${date}`;
}

/**
 * The fields where the entries of a duplicate group differ, with each
 * distinct value; fields where all agree (or only one has a value) are
 * taken as they are.
 */
export function mergeConflicts(entries: readonly AutofillEntryRow[]): { field: string; values: string[] }[] {
  const conflicts: { field: string; values: string[] }[] = [];
  for (const def of kAllFields) {
    const values = [...new Set(entries.map((e) => e.values[def.name] ?? '').filter((v) => v !== ''))];
    if (values.length > 1) conflicts.push({ field: def.name, values });
  }
  return conflicts;
}

/** The merged values: the chosen value for each conflict, else the one value. */
export function mergedValues(entries: readonly AutofillEntryRow[], chosen: Readonly<Record<string, string>>): Record<string, string> {
  const out: Record<string, string> = {};
  for (const def of kAllFields) {
    const value = chosen[def.name] ?? entries.map((e) => e.values[def.name] ?? '').find((v) => v !== '') ?? '';
    if (value !== '') out[def.name] = value;
  }
  return out;
}

/** Label words typed as "číslo smlouvy, contract no". */
export function parseLabelWords(text: string): string[] {
  return text.split(',').map((w) => w.trim()).filter((w) => w !== '');
}
