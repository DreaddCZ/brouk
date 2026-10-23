import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BridgeError } from '../shared/bridge.js';
import { HistoryApi, parsePages, parseProblems, parseSites, parseSql, parseState, parseVisits } from './api.js';

test('state, sites, pages and visits are checked', () => {
  const state = parseState({ profiles: [{ id: 'default', name: 'Main', color: 'blue' }], view: 'day' });
  assert.equal(state.view, 'day');
  assert.equal(parseState({ profiles: [], view: 'other' }).view, 'site');
  assert.throws(() => parseState({ profiles: [{ id: 1 }] }), BridgeError);

  const sites = parseSites({ sites: [{ profile: 'default', host: 'a.com', visits: 3, pages: 2, lastVisit: 10 }] });
  assert.equal(sites[0]?.pages, 2);
  assert.throws(() => parseSites({ sites: [{ profile: 'default', host: 'a.com', visits: -1, pages: 2, lastVisit: 10 }] }), BridgeError);
  assert.throws(() => parseSites({}), BridgeError);

  assert.equal(parsePages({ pages: [{ url: 'https://a.com/', title: 'A', visits: 1, lastVisit: 5 }] })[0]?.title, 'A');
  const visits = parseVisits({ visits: [{ profile: 'p', id: 7, url: 'u', title: '', host: '', time: 9, transition: 'typed' }] });
  assert.equal(visits[0]?.id, 7);
  assert.throws(() => parseVisits({ visits: [{ profile: 'p', id: 7 }] }), BridgeError);
});

test('requests carry the profile only when one is chosen', async () => {
  const sent: unknown[] = [];
  const api = new HistoryApi((request) => {
    sent.push(JSON.parse(request));
    return Promise.resolve(JSON.stringify({ sites: [], visits: [] }));
  });
  await api.sites(null);
  await api.sites('p-work');
  await api.visits(null, 0, 200);
  await api.sites(null, 'site:a.com');
  await api.sites(null, '   ');
  await api.remove([{ profile: 'default', kind: 'visit', id: 3 }]);
  assert.deepEqual(sent, [
    { type: 'history.sites' },
    { type: 'history.sites', profile: 'p-work' },
    { type: 'history.visits', before: 0, limit: 200 },
    { type: 'history.sites', query: 'site:a.com' },
    { type: 'history.sites' },
    { type: 'history.delete', items: [{ profile: 'default', kind: 'visit', id: 3 }] },
  ]);
});

test('problems of a search are read when present', () => {
  assert.deepEqual(parseProblems({ sites: [] }), []);
  assert.deepEqual(parseProblems({ problems: [{ start: 1, end: 4, message: 'x' }] }), [{ start: 1, end: 4, message: 'x' }]);
  assert.throws(() => parseProblems({ problems: [{ start: 1 }] }), BridgeError);
});

test('SQL results are checked', () => {
  const result = parseSql({ columns: ['url', 'n'], rows: [['https://a.com/', 3], [null, 1.5]], truncated: true });
  assert.equal(result.rows[1]?.[0], null);
  assert.equal(result.truncated, true);
  assert.throws(() => parseSql({ columns: ['a'], rows: [[1, 2]], truncated: false }), BridgeError);
  assert.throws(() => parseSql({ columns: ['a'], rows: [[{}]], truncated: false }), BridgeError);
  assert.throws(() => parseSql({ columns: ['a'], rows: [] }), BridgeError);
});
