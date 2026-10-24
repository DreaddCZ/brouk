import { test } from 'node:test';
import assert from 'node:assert/strict';
import { groupLists, isListUrl, listStatus } from './adblock_logic.js';
import { parseAdblockState, parseProblems } from './adblock_api.js';
import type { FilterList } from './adblock_api.js';

function list(id: string, category: string, extra: Partial<FilterList> = {}): FilterList {
  return { id, name: id, category, url: `https://x/${id}`, enabled: true, custom: false, updated: 0, error: '', ...extra };
}

test('lists are grouped in the order of the categories', () => {
  const groups = groupLists(
    [{ id: 'ads', label: 'Ads' }, { id: 'privacy', label: 'Privacy' }, { id: 'custom', label: 'Custom' }],
    [list('b', 'privacy'), list('a', 'ads'), list('c', 'ads')],
  );
  assert.deepEqual(groups.map((g) => [g.category.id, g.lists.map((l) => l.id)]), [['ads', ['a', 'c']], ['privacy', ['b']]]);
});

test('list status: updated, never, or the error', () => {
  const now = 1_000_000;
  assert.equal(listStatus(list('a', 'ads', { updated: now - 3 * 3600 }), now), 'Updated 3 hours ago');
  assert.equal(listStatus(list('a', 'ads', { updated: now - 30 }), now), 'Updated just now');
  assert.equal(listStatus(list('a', 'ads', { updated: now - 5 * 86400 }), now), 'Updated 5 days ago');
  assert.equal(listStatus(list('a', 'ads'), now), 'Not downloaded yet');
  assert.equal(listStatus(list('a', 'ads', { enabled: false }), now), '');
  assert.equal(
    listStatus(list('a', 'ads', { error: 'The server answered 404 instead of the list.', updated: now - 7200 }), now),
    'The server answered 404 instead of the list. (keeps the version from 2 hours ago)',
  );
});

test('list addresses', () => {
  assert.ok(isListUrl('https://example.com/list.txt'));
  assert.ok(isListUrl('  http://example.com/hosts '));
  assert.ok(!isListUrl('ftp://example.com/x'));
  assert.ok(!isListUrl('example.com/list.txt'));
});

test('answers are checked', () => {
  const state = parseAdblockState({
    categories: [{ id: 'ads', label: 'Ads' }],
    lists: [{ id: 'easylist', name: 'EasyList', category: 'ads', url: 'https://e', enabled: true, custom: false, updated: 5, error: '' }],
    updating: true,
    ready: true,
  });
  assert.equal(state.lists[0]?.updated, 5);
  assert.equal(state.updating, true);
  assert.throws(() => parseAdblockState({ categories: [], lists: [{ id: 1 }] }));
  assert.deepEqual(parseProblems({ problems: [{ line: 3, error: 'Bad' }] }), [{ line: 3, error: 'Bad' }]);
  assert.throws(() => parseProblems({ problems: [{ line: 0, error: 'x' }] }));
});
