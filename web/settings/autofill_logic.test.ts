import { test } from 'node:test';
import assert from 'node:assert/strict';
import { addressLine, fullName, mergeConflicts, mergedValues, originLine, parseLabelWords, searchEntries, sortEntries } from './autofill_logic.js';
import { parseAutofillState } from './autofill_api.js';
import type { AutofillEntryRow } from './autofill_api.js';

function entry(id: number, values: Record<string, string>, extra: Partial<AutofillEntryRow> = {}): AutofillEntryRow {
  return { id, label: `E${id}`, values, problems: {}, originSite: '', created: 0, lastUsed: 0, timesUsed: 0, ...extra };
}

test('names and address lines', () => {
  const e = entry(1, { first_name: 'Radek', last_name: 'Ševčík', street: 'Karlova 1\nPatro 2', postal_code: '110 00', city: 'Praha' });
  assert.equal(fullName(e), 'Radek Ševčík');
  assert.equal(addressLine(e), 'Karlova 1, Patro 2, 110 00 Praha');
});

test('search ignores case and accents', () => {
  const all = [entry(1, { city: 'Praha' }), entry(2, { last_name: 'Ševčík' })];
  assert.deepEqual(searchEntries(all, 'sevcik').map((e) => e.id), [2]);
  assert.deepEqual(searchEntries(all, 'PRAHA').map((e) => e.id), [1]);
  assert.equal(searchEntries(all, '').length, 2);
});

test('sorting', () => {
  const all = [entry(1, {}, { label: 'b', lastUsed: 5, timesUsed: 1 }), entry(2, {}, { label: 'a', lastUsed: 9, timesUsed: 0 })];
  assert.deepEqual(sortEntries(all, 'last_used').map((e) => e.id), [2, 1]);
  assert.deepEqual(sortEntries(all, 'label').map((e) => e.id), [2, 1]);
  assert.deepEqual(sortEntries(all, 'times_used').map((e) => e.id), [1, 2]);
});

test('origin line', () => {
  assert.equal(originLine(entry(1, {})), 'added by hand');
  assert.equal(originLine(entry(1, {}, { originSite: 'eshop.cz', created: 1_761_177_600 })), 'saved from eshop.cz, 2025-10-23');
});

test('merging keeps the chosen value per field', () => {
  const group = [entry(1, { city: 'Praha', phone: '777' }), entry(2, { city: 'Praha 1', email: 'a@b.cz' })];
  assert.deepEqual(mergeConflicts(group), [{ field: 'city', values: ['Praha', 'Praha 1'] }]);
  assert.deepEqual(mergedValues(group, { city: 'Praha 1' }), { phone: '777', email: 'a@b.cz', city: 'Praha 1' });
});

test('label words and state parsing', () => {
  assert.deepEqual(parseLabelWords(' číslo smlouvy, contract no ,'), ['číslo smlouvy', 'contract no']);
  assert.throws(() => parseAutofillState({ loaded: true, error: '', country: 'CZ', entries: [{ id: 'x' }], custom: [], canUndo: false }));
});
