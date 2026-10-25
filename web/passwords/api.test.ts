import { test } from 'node:test';
import assert from 'node:assert/strict';
import { LockedError, PasswordsApi, parseCheckup, parseEntry, parseImport, parseState } from './api.js';

const row = {
  profile: 'p', id: 7, origin: 'https://a.com', others: ['https://b.com'], username: 'me', hasNote: true, hasTotp: false,
  created: 1, lastUsed: 2, changed: 3, timesUsed: 4,
};

test('answers are checked', () => {
  assert.equal(parseEntry(row).others[0], 'https://b.com');
  assert.throws(() => parseEntry({ ...row, id: -1 }));
  assert.throws(() => parseEntry({ ...row, others: [1] }));
  assert.equal(parseState({ profiles: [{ id: 'p', name: 'P', color: 'blue', bitwarden: false }], currentProfile: 'p',
    unlockedFor: 0, breachCheck: true }).profiles.length, 1);
  assert.equal(parseImport(null), null);
  assert.throws(() => parseCheckup({ entries: [{ ...row, password: 'x', breached: 0 }], reused: [[0, 5]] }));
});

test('a locked answer becomes LockedError', async () => {
  const api = new PasswordsApi(() => Promise.reject(new (class extends Error { override name = 'BridgeError'; })('Locked.')));
  // A plain Error is passed on unchanged.
  await assert.rejects(api.reveal('p', 1), /Locked/);
  const { BridgeError } = await import('../shared/bridge.js');
  const locked = new PasswordsApi(() => Promise.reject(new BridgeError('Locked.')));
  await assert.rejects(locked.reveal('p', 1), LockedError);
});

test('requests carry their arguments', async () => {
  const sent: unknown[] = [];
  const api = new PasswordsApi((request) => {
    sent.push(JSON.parse(request));
    return Promise.resolve('');
  });
  await api.copy('p', 3, 'password');
  assert.deepEqual(sent[0], { profile: 'p', id: 3, field: 'password', type: 'passwords.copy' });
});
