import { test } from 'node:test';
import assert from 'node:assert/strict';
import { autofillKind, classifyAddressForm } from './address_rules.js';
import type { FieldInfo } from './field_rules.js';

function field(extra: Partial<FieldInfo>): FieldInfo {
  return {
    type: 'text', autocomplete: '', name: '', id: '', label: '', placeholder: '',
    inputMode: '', maxLength: -1, visible: true, ...extra,
  };
}

test('autocomplete attributes', () => {
  assert.equal(autofillKind(field({ autocomplete: 'given-name' })), 'first_name');
  assert.equal(autofillKind(field({ autocomplete: 'shipping postal-code' })), 'postal_code');
  assert.equal(autofillKind(field({ autocomplete: 'address-level2' })), 'city');
  assert.equal(autofillKind(field({ type: 'email' })), 'email');
});

test('Czech checkout form', () => {
  const kinds = classifyAddressForm([
    field({ label: 'Jméno' }),
    field({ label: 'Příjmení' }),
    field({ label: 'Ulice a č.p.' }),
    field({ label: 'Město' }),
    field({ label: 'PSČ' }),
    field({ label: 'Telefon' }),
    field({ label: 'Název firmy' }),
    field({ label: 'IČO' }),
    field({ label: 'DIČ' }),
  ]);
  assert.deepEqual(kinds, ['first_name', 'last_name', 'street', 'city', 'postal_code', 'phone', 'company', 'ico', 'dic']);
});

test('English invoicing form', () => {
  assert.deepEqual(classifyAddressForm([
    field({ name: 'full_name' }),
    field({ name: 'company_name' }),
    field({ name: 'address1' }),
    field({ name: 'address2' }),
    field({ name: 'zip' }),
    field({ name: 'vat_number' }),
    field({ name: 'iban' }),
    field({ name: 'swift' }),
  ]), ['full_name', 'company', 'street', 'street_line2', 'postal_code', 'vat_id', 'iban', 'bic']);
});

test('other fields are not autofill fields', () => {
  assert.equal(autofillKind(field({ name: 'q', placeholder: 'Search' })), null);
  assert.equal(autofillKind(field({ name: 'message' })), null);
  assert.equal(autofillKind(field({ name: 'password', type: 'password' })), null);
  assert.equal(autofillKind(field({ name: 'name', visible: false })), null);
  assert.equal(autofillKind(field({ label: 'Uživatelské jméno' })), null);
});

test('a repeated kind is not filled twice', () => {
  assert.deepEqual(classifyAddressForm([field({ label: 'City' }), field({ label: 'City (again)' })]), ['city', null]);
});
