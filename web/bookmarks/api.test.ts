import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BridgeError } from '../shared/bridge.js';
import { BookmarksApi, parseImport, parseNode, parseState } from './api.js';

const state = {
  profiles: [{ id: 'work', name: 'Work', color: 'blue' }],
  currentProfile: 'work',
  bar: {
    id: 1, type: 'folder', name: 'Bookmarks bar', profile: '', effective: '', added: 0,
    children: [{ id: 3, type: 'url', name: 'A', url: 'https://a.example/', profile: 'work', effective: 'work', added: 5 }],
  },
  other: { id: 2, type: 'folder', name: 'Other bookmarks', profile: '', effective: '', added: 0, children: [] },
};

test('the state is read and checked', () => {
  const parsed = parseState(state);
  assert.equal(parsed.currentProfile, 'work');
  assert.equal(parsed.tree.bar.children[0]?.url, 'https://a.example/');
  assert.equal(parsed.tree.bar.children[0]?.effective, 'work');
  assert.deepEqual(parsed.tree.other.children, []);
});

test('broken answers are rejected', () => {
  assert.throws(() => parseNode({ id: 1, type: 'weird', name: '', profile: '', effective: '', added: 0 }), BridgeError);
  assert.throws(() => parseNode({ id: 1, type: 'folder', name: '', profile: '', effective: '', added: 0 }), BridgeError);
  assert.throws(() => parseNode({ id: -1, type: 'url', name: '', url: '', profile: '', effective: '', added: 0 }), BridgeError);
  assert.throws(() => parseState({ ...state, bar: null }), BridgeError);
  assert.throws(() => parseImport({}), BridgeError);
  let deep: unknown = { id: 9, type: 'url', name: '', url: 'x', profile: '', effective: '', added: 0 };
  for (let i = 0; i < 1100; i++) deep = { id: i + 10, type: 'folder', name: '', profile: '', effective: '', added: 0, children: [deep] };
  assert.throws(() => parseNode(deep), BridgeError);
});

test('requests carry their arguments', async () => {
  const sent: unknown[] = [];
  const api = new BookmarksApi(async (request) => {
    sent.push(JSON.parse(request));
    return '{"id": 42, "undone": true, "saved": true}';
  }, null);
  assert.equal(await api.add({ parent: 1, index: null, folder: false, name: 'A', url: 'https://a/', profile: '' }), 42);
  await api.move([3, 4], 2, 0);
  await api.edit({ id: 3, name: 'B', url: 'https://b/', profile: 'work', parent: null });
  assert.equal(await api.undo(), true);
  assert.deepEqual(sent, [
    { parent: 1, folder: false, name: 'A', url: 'https://a/', profile: '', type: 'bookmarks.add' },
    { ids: [3, 4], parent: 2, index: 0, type: 'bookmarks.move' },
    { id: 3, name: 'B', url: 'https://b/', profile: 'work', type: 'bookmarks.edit' },
    { type: 'bookmarks.undo' },
  ]);
});
