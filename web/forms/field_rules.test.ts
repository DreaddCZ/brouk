import { test } from 'node:test';
import assert from 'node:assert/strict';
import { classifyForm, isOtpField } from './field_rules.js';
import type { FieldInfo } from './field_rules.js';

function field(extra: Partial<FieldInfo>): FieldInfo {
  return {
    type: 'text', autocomplete: '', name: '', id: '', label: '', placeholder: '',
    inputMode: '', maxLength: -1, visible: true, ...extra,
  };
}

test('a plain login form', () => {
  assert.deepEqual(classifyForm([
    field({ name: 'login' }),
    field({ type: 'password', name: 'pass' }),
    field({ type: 'checkbox', name: 'remember' }),
  ]), ['username', 'password', null]);
});

test('autocomplete attributes win', () => {
  assert.deepEqual(classifyForm([
    field({ name: 'q', autocomplete: 'off' }),
    field({ name: 'x', autocomplete: 'username' }),
    field({ type: 'password', autocomplete: 'new-password' }),
  ]), [null, 'username', 'new-password']);
  assert.deepEqual(classifyForm([
    field({ type: 'email', autocomplete: 'email' }),
    field({ type: 'password', autocomplete: 'current-password' }),
  ]), ['username', 'password']);
});

test('sign-up and password change forms', () => {
  assert.deepEqual(classifyForm([
    field({ type: 'email', name: 'email' }),
    field({ type: 'password', name: 'password' }),
    field({ type: 'password', name: 'password2' }),
  ]), ['username', 'new-password', 'new-password']);
  assert.deepEqual(classifyForm([
    field({ type: 'password', name: 'a' }),
    field({ type: 'password', name: 'b' }),
    field({ type: 'password', name: 'c' }),
  ]), ['password', 'new-password', 'new-password']);
  assert.deepEqual(classifyForm([
    field({ type: 'password', label: 'Současné heslo' }),
    field({ type: 'password', label: 'Nové heslo' }),
  ]), ['password', 'new-password']);
});

test('Czech labels', () => {
  assert.deepEqual(classifyForm([
    field({ label: 'Přihlašovací jméno' }),
    field({ type: 'password', label: 'Heslo' }),
  ]), ['username', 'password']);
  assert.deepEqual(classifyForm([
    field({ type: 'password', label: 'Heslo' }),
    field({ type: 'password', label: 'Heslo znovu' }),
  ]), ['new-password', 'new-password']);
});

test('hidden fields are not login fields', () => {
  assert.deepEqual(classifyForm([
    field({ name: 'user', visible: false }),
    field({ name: 'user2' }),
    field({ type: 'password', visible: false }),
    field({ type: 'password' }),
  ]), [null, 'username', null, 'password']);
});

test('first step of a multi-step login', () => {
  // Microsoft and Google.
  assert.deepEqual(classifyForm([field({ type: 'email', name: 'loginfmt', autocomplete: 'username' })]), ['username']);
  assert.deepEqual(classifyForm([field({ type: 'email', id: 'identifierId', autocomplete: 'username webauthn' })]), ['username']);
  // A newsletter box or a search field is not a login.
  assert.deepEqual(classifyForm([field({ type: 'email', name: 'newsletter_email' })]), [null]);
  assert.deepEqual(classifyForm([field({ type: 'search', name: 'q' })]), [null]);
  // A contact form with many fields is not a login either.
  assert.deepEqual(classifyForm([
    field({ name: 'name' }), field({ type: 'email', name: 'email' }), field({ name: 'subject' }),
  ]), [null, null, null]);
});

test('one-time code fields', () => {
  assert.ok(isOtpField(field({ autocomplete: 'one-time-code' })));
  assert.ok(isOtpField(field({ name: 'totp' })));
  assert.ok(isOtpField(field({ label: 'Ověřovací kód' })));
  assert.ok(isOtpField(field({ name: 'code', inputMode: 'numeric', maxLength: 6 })));
  assert.ok(!isOtpField(field({ name: 'postal_code' })));
  assert.ok(!isOtpField(field({ type: 'password', name: 'otp' })));
  assert.deepEqual(classifyForm([field({ name: 'otp', inputMode: 'numeric' })]), ['otp']);
});
