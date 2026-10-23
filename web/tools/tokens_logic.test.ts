import { test } from 'node:test';
import assert from 'node:assert/strict';
import { decodeJwt, findJwts, isExpired, relativeTime, summarizeClaims } from './tokens_logic.js';

const b64 = (value: unknown): string =>
  btoa(String.fromCharCode(...new TextEncoder().encode(JSON.stringify(value))))
    .replace(/=+$/, '').replace(/\+/g, '-').replace(/\//g, '_');
const token = `${b64({ alg: 'RS256', typ: 'JWT' })}.${b64({
  iss: 'https://login.example.com', aud: ['api', 'web'], tid: 't-1', sub: 'u', name: 'Radek Ševčík',
  scp: 'read write', iat: 1000, exp: 4600,
})}.sig`;

test('decodes JWTs, including UTF-8 claims', () => {
  const decoded = decodeJwt(token);
  assert.equal(decoded?.header['alg'], 'RS256');
  assert.equal(decoded?.payload['name'], 'Radek Ševčík');
  assert.equal(decodeJwt('opaque-token'), null);
  assert.equal(decodeJwt('a.b.c'), null);
});

test('finds JWTs in text', () => {
  assert.deepEqual(findJwts(`Bearer ${token}`), [token]);
  assert.deepEqual(findJwts(`x${token}`), []);
  assert.deepEqual(findJwts('nothing'), []);
});

test('relative times', () => {
  assert.equal(relativeTime(1720, 1000), 'in 12 min');
  assert.equal(relativeTime(1000 - 3 * 3600, 1000), '3 h ago');
  assert.equal(relativeTime(1000 + 5 * 86400, 1000), 'in 5 days');
  assert.equal(relativeTime(1030, 1000), 'in 30 s');
});

test('claims and expiry', () => {
  const payload = decodeJwt(token)?.payload ?? {};
  const claims = summarizeClaims(payload, 1000, 'en-US');
  assert.deepEqual(claims.slice(0, 6).map((c) => [c.label, c.value]), [
    ['Issuer', 'https://login.example.com'], ['Audience', 'api, web'], ['Tenant', 't-1'], ['Subject', 'u'],
    ['Name', 'Radek Ševčík'], ['Scopes', 'read write'],
  ]);
  assert.ok(claims.find((c) => c.label === 'Expires')?.value.endsWith('(in 1 h)'));
  assert.equal(isExpired(payload, 1000), false);
  assert.equal(isExpired(payload, 5000), true);
});
