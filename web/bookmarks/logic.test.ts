import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  clickSelection,
  displayName,
  dropTarget,
  folders,
  indexTree,
  isInside,
  pathLabel,
  search,
  selectionLabel,
  visibleChildren,
  type BookmarkNode,
  type BookmarkTree,
} from './logic.js';

function url(id: number, name: string, address: string, effective = '', profile = effective): BookmarkNode {
  return { id, type: 'url', name, url: address, profile, effective, added: 0, children: [] };
}
function dir(id: number, name: string, children: BookmarkNode[], effective = '', profile = effective): BookmarkNode {
  return { id, type: 'folder', name, url: '', profile, effective, added: 0, children };
}

// Bar: Jira (work), Work folder (work) with Wiki (inherited), Docs folder
// (general) with a home bookmark. Other: News.
const tree: BookmarkTree = {
  bar: dir(1, 'Bookmarks bar', [
    url(3, 'Jira board', 'https://jira.example/board', 'work'),
    dir(4, 'Work', [url(5, 'Wiki', 'https://wiki.example/', 'work', '')], 'work'),
    dir(6, 'Docs', [url(7, 'Recipes', 'https://food.example/', 'home')]),
  ]),
  other: dir(2, 'Other bookmarks', [url(8, '', 'https://news.example/')]),
};
const index = indexTree(tree);

test('the index finds items and their folders', () => {
  assert.equal(index.get(5)?.parent?.id, 4);
  assert.equal(index.get(1)?.parent, null);
  assert.ok(isInside(index, 5, 4));
  assert.ok(isInside(index, 5, 1));
  assert.ok(!isInside(index, 4, 5));
  assert.equal(pathLabel(index, 5), 'Bookmarks bar › Work');
});

test('the profile filter keeps folders that contain matches', () => {
  const all = { kind: 'all' } as const;
  const general = { kind: 'general' } as const;
  const home = { kind: 'profile', id: 'home' } as const;
  assert.deepEqual(visibleChildren(tree.bar, all).map((n) => n.id), [3, 4, 6]);
  assert.deepEqual(visibleChildren(tree.bar, general).map((n) => n.id), [6]);
  assert.deepEqual(visibleChildren(tree.bar, home).map((n) => n.id), [6]);
  assert.deepEqual(visibleChildren(tree.bar, { kind: 'profile', id: 'work' }).map((n) => n.id), [3, 4]);
});

test('search matches every word in name or address', () => {
  const all = { kind: 'all' } as const;
  assert.deepEqual(search(tree, 'JIRA board', all).map((n) => n.id), [3]);
  assert.deepEqual(search(tree, 'example', all).map((n) => n.id), [3, 5, 7, 8]);
  assert.deepEqual(search(tree, 'work', all).map((n) => n.id), [4]);
  assert.deepEqual(search(tree, 'example', { kind: 'profile', id: 'home' }).map((n) => n.id), [7]);
  assert.deepEqual(search(tree, '   ', all), []);
});

test('folders in tree order, without one and its contents', () => {
  assert.deepEqual(folders(tree).map((f) => [f.node.id, f.depth]), [[1, 0], [4, 1], [6, 1], [2, 0]]);
  assert.deepEqual(folders(tree, 4).map((f) => f.node.id), [1, 6, 2]);
});

test('a bookmark without a name shows its address', () => {
  assert.equal(displayName(url(1, '', 'https://a.example/')), 'https://a.example/');
  assert.equal(displayName(url(1, 'A', 'https://a.example/')), 'A');
});

test('clicks select one, ⌘ toggles, Shift selects a range', () => {
  const order = [3, 4, 6, 8];
  assert.deepEqual([...clickSelection(new Set([3]), order, 3, 6, { toggle: false, range: false })], [6]);
  assert.deepEqual([...clickSelection(new Set([3]), order, 3, 6, { toggle: true, range: false })], [3, 6]);
  assert.deepEqual([...clickSelection(new Set([3, 6]), order, 3, 6, { toggle: true, range: false })], [3]);
  assert.deepEqual([...clickSelection(new Set([3]), order, 3, 6, { toggle: false, range: true })], [3, 4, 6]);
  assert.deepEqual([...clickSelection(new Set([8]), order, 6, 3, { toggle: true, range: true })].sort(), [3, 4, 6, 8]);
  assert.deepEqual([...clickSelection(new Set(), order, null, 4, { toggle: false, range: true })], [4]);
});

test('drops go before, after or into a row', () => {
  assert.deepEqual(dropTarget(tree.bar, tree.bar.children[1]!, 'before'), { parent: 1, index: 1 });
  assert.deepEqual(dropTarget(tree.bar, tree.bar.children[1]!, 'after'), { parent: 1, index: 2 });
  assert.deepEqual(dropTarget(tree.bar, tree.bar.children[1]!, 'into'), { parent: 4, index: null });
  assert.deepEqual(dropTarget(tree.bar, tree.bar.children[0]!, 'into'), { parent: 1, index: 0 });
});

test('selection labels', () => {
  assert.equal(selectionLabel(1), '1 item selected');
  assert.equal(selectionLabel(3), '3 items selected');
});
