import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BridgeError } from '../shared/bridge.js';
import { ProfilesApi, parseImportResult, parseState } from './api.js';

const state = {
  profiles: [
    { id: 'default', name: 'Main', color: 'blue', isDefault: true, openTabs: 3, links: [] },
    {
      id: 'p-12345678', name: 'Work', color: 'green', isDefault: false, openTabs: 0,
      links: [{ pattern: 'portal.azure.com', sharedWith: ['Main'] }],
    },
  ],
  colors: ['grey', 'blue', 'green'],
  showAtStartup: false,
  linksReady: true,
};

test('parseState accepts a valid state', () => {
  const parsed = parseState(state);
  assert.equal(parsed.profiles.length, 2);
  assert.equal(parsed.profiles[0]?.name, 'Main');
  assert.deepEqual(parsed.colors, ['grey', 'blue', 'green']);
});

test('parseState reads each profile\'s links', () => {
  const parsed = parseState(state);
  assert.deepEqual(parsed.profiles[1]?.links, [{ pattern: 'portal.azure.com', sharedWith: ['Main'] }]);
  assert.equal(parsed.linksReady, true);
  assert.throws(
    () => parseState({ ...state, profiles: [{ ...state.profiles[0], links: [{ pattern: 1, sharedWith: [] }] }] }),
    BridgeError,
  );
});

test('setLinks returns the problems', async () => {
  const api = new ProfilesApi(async () => '{"problems":[{"index":1,"message":"Not a valid pattern."}]}');
  assert.deepEqual(await api.setLinks('p-12345678', ['a.com', 'bad pattern']), [
    { index: 1, message: 'Not a valid pattern.' },
  ]);
});

test('parseState rejects wrong shapes', () => {
  assert.throws(() => parseState(null), BridgeError);
  assert.throws(() => parseState({ ...state, showAtStartup: 'no' }), BridgeError);
  assert.throws(() => parseState({ ...state, colors: [1] }), BridgeError);
  assert.throws(
    () => parseState({ ...state, profiles: [{ ...state.profiles[0], openTabs: -1 }] }),
    BridgeError,
  );
  assert.throws(
    () => parseState({ ...state, profiles: [{ ...state.profiles[0], name: 7 }] }),
    BridgeError,
  );
});

test('ProfilesApi sends typed messages', async () => {
  const sent: unknown[] = [];
  const api = new ProfilesApi(async (request) => {
    const message: unknown = JSON.parse(request);
    sent.push(message);
    if (request.includes('profiles.create')) return '{"id":"p-87654321"}';
    if (request.includes('profiles.delete')) return '{"deleted":false}';
    return '';
  });
  assert.equal(await api.create('Client A', 'red'), 'p-87654321');
  await api.update('p-87654321', 'Client B', 'red');
  await api.setShowAtStartup(true);
  assert.equal(await api.remove('p-87654321'), false);
  assert.deepEqual(sent, [
    { name: 'Client A', color: 'red', type: 'profiles.create' },
    { id: 'p-87654321', name: 'Client B', color: 'red', type: 'profiles.update' },
    { show: true, type: 'profiles.setShowAtStartup' },
    { id: 'p-87654321', type: 'profiles.delete' },
  ]);
});

test('ProfilesApi rejects malformed answers and passes on errors', async () => {
  const bad = new ProfilesApi(async () => '{"id":5}');
  await assert.rejects(bad.create('A', 'red'), BridgeError);
  const failing = new ProfilesApi(async () => {
    throw new BridgeError('Another profile is already called “A”.');
  });
  await assert.rejects(failing.create('A', 'red'), /already called/);
});

test('import and export answers', async () => {
  assert.deepEqual(parseImportResult({ id: 'p-1', problems: ['x'] }), {
    id: 'p-1',
    problems: ['x'],
  });
  assert.deepEqual(parseImportResult({ id: null, problems: [] }), { id: null, problems: [] });
  assert.throws(() => parseImportResult({ id: 3, problems: [] }), BridgeError);
  assert.throws(() => parseImportResult({ id: null, problems: [1] }), BridgeError);
  const api = new ProfilesApi(async (request) =>
    request.includes('profiles.export') ? '{"saved":true}' : '{"id":null,"problems":[]}',
  );
  assert.equal(await api.exportProfile('p-1'), true);
  assert.deepEqual(await api.importProfile(), { id: null, problems: [] });
});
