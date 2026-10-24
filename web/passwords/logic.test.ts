import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ago, checkupResult, displayHost, filterEntries, parseOthers } from './logic.js';
import type { CheckupEntry, Entry } from './api.js';

function entry(profile: string, id: number, origin: string, username: string): Entry {
  return { profile, id, origin, others: [], username, hasNote: false, hasTotp: false, created: 0, lastUsed: 0, changed: 0,
    timesUsed: 0 };
}

test('hosts are shown without scheme and www', () => {
  assert.equal(displayHost('https://www.example.com'), 'example.com');
  assert.equal(displayHost('http://localhost:8080'), 'localhost:8080');
});

test('filtering by profile and words, sorted by site', () => {
  const all = [
    entry('work', 1, 'https://www.b.com', 'me'),
    entry('home', 2, 'https://a.com', 'x'),
    entry('work', 3, 'https://a.com', 'boss'),
  ];
  assert.deepEqual(filterEntries(all, '', '').map((e) => e.id), [3, 2, 1]);
  assert.deepEqual(filterEntries(all, 'work', '').map((e) => e.id), [3, 1]);
  assert.deepEqual(filterEntries(all, '', 'A.COM boss').map((e) => e.id), [3]);
});

test('times ago', () => {
  const now = 100 * 86400;
  assert.equal(ago(0, now), 'never');
  assert.equal(ago(now - 10, now), 'today');
  assert.equal(ago(now - 86400, now), 'yesterday');
  assert.equal(ago(now - 5 * 86400, now), '5 days ago');
  assert.equal(ago(now - 90 * 86400, now), '3 months ago');
});

test('other sites field', () => {
  assert.deepEqual(parseOthers(' a.com, b.com\nc.com ,, '), ['a.com', 'b.com', 'c.com']);
});

test('checkup lists', () => {
  const e = (id: number, breached: number): CheckupEntry => ({ ...entry('p', id, 'https://x.com', 'u'), password: 'pw',
    breached });
  const result = checkupResult({ entries: [e(1, 0), e(2, 5), e(3, 50)], reused: [[0, 2]] }, [0, 4, 3]);
  assert.deepEqual(result.weak.map((x) => x.id), [1]);
  assert.deepEqual(result.breached.map((x) => x.id), [3, 2]);
  assert.deepEqual(result.reused.map((g) => g.map((x) => x.id)), [[1, 3]]);
});
