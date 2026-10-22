import { test } from 'node:test';
import assert from 'node:assert/strict';
import type { SettingItem } from './api.js';
import { isShown, sections } from './logic.js';

function item(key: string, section: string, value: string | boolean = ''): SettingItem {
  return {
    key,
    type: typeof value === 'boolean' ? 'bool' : 'string',
    format: 'text',
    section,
    label: key,
    description: '',
    restart: false,
    choices: [],
    value,
  };
}

test('sections keep the order of first appearance', () => {
  const grouped = sections([item('a', 'One'), item('b', 'Two'), item('c', 'One')]);
  assert.deepEqual(
    grouped.map((s) => [s.title, s.items.map((i) => i.key)]),
    [['One', ['a', 'c']], ['Two', ['b']]],
  );
});

test('the custom search address shows only for a custom engine', () => {
  const custom = item('search.custom_template', 'Search');
  assert.equal(isShown(custom, [item('search.engine', 'Search', 'google'), custom]), false);
  assert.equal(isShown(custom, [item('search.engine', 'Search', 'custom'), custom]), true);
  const filled = item('search.custom_template', 'Search', 'https://x/?q=%s');
  assert.equal(isShown(filled, [item('search.engine', 'Search', 'google'), filled]), true);
});
