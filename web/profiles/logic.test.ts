import { test } from 'node:test';
import assert from 'node:assert/strict';
import type { Profile } from './api.js';
import { colorLabel, nameProblem, tabsLabel } from './logic.js';

const profiles: Profile[] = [
  { id: 'default', name: 'Main', color: 'blue', isDefault: true, openTabs: 1, links: [] },
  { id: 'p-1', name: 'Klient Č', color: 'red', isDefault: false, openTabs: 0, links: [] },
];

test('names must be non-empty, short, plain and unique', () => {
  assert.equal(nameProblem('  ', profiles, null), 'Enter a name.');
  assert.equal(nameProblem('x'.repeat(65), profiles, null), 'The name is too long.');
  assert.equal(nameProblem('Č'.repeat(33), profiles, null), 'The name is too long.');
  assert.ok(nameProblem('a\nb', profiles, null)?.includes('line breaks'));
  assert.equal(nameProblem('main', profiles, null), 'Another profile is already called “Main”.');
  assert.equal(nameProblem('klient č', profiles, null), 'Another profile is already called “Klient Č”.');
  assert.equal(nameProblem('Main', profiles, 'default'), null);
  assert.equal(nameProblem(' Work ', profiles, null), null);
});

test('labels', () => {
  assert.equal(colorLabel('blue'), 'Blue');
  assert.equal(tabsLabel(0), 'No open tabs');
  assert.equal(tabsLabel(1), '1 open tab');
  assert.equal(tabsLabel(4), '4 open tabs');
});
