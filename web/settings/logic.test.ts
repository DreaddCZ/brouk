import { test } from 'node:test';
import assert from 'node:assert/strict';
import type { SettingItem } from './api.js';
import { isShown, sections, shortcutFromKey, type KeyPress } from './logic.js';

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
    min: 0,
    max: 0,
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

function press(key: string, code: string, mods: Partial<KeyPress> = {}): KeyPress {
  return { key, code, metaKey: false, ctrlKey: false, altKey: false, shiftKey: false, ...mods };
}

test('key presses become shortcuts as the settings file writes them', () => {
  assert.equal(shortcutFromKey(press('t', 'KeyT', { metaKey: true })), 'cmd+t');
  assert.equal(shortcutFromKey(press('T', 'KeyT', { metaKey: true, shiftKey: true })), 'shift+cmd+t');
  // Option types "ˆ"; the key is still I.
  assert.equal(shortcutFromKey(press('ˆ', 'KeyI', { metaKey: true, altKey: true })), 'opt+cmd+i');
  assert.equal(shortcutFromKey(press('Tab', 'Tab', { ctrlKey: true, shiftKey: true })), 'ctrl+shift+tab');
  assert.equal(shortcutFromKey(press('ArrowRight', 'ArrowRight', { metaKey: true, altKey: true })), 'opt+cmd+right');
  assert.equal(shortcutFromKey(press('}', 'BracketRight', { metaKey: true, shiftKey: true })), 'cmd+}');
  assert.equal(shortcutFromKey(press('“', 'BracketLeft', { metaKey: true, altKey: true })), 'opt+cmd+[');
  assert.equal(shortcutFromKey(press(',', 'Comma', { metaKey: true })), 'cmd+comma');
  assert.equal(shortcutFromKey(press('+', 'Equal', { metaKey: true, shiftKey: true })), 'cmd+plus');
  assert.equal(shortcutFromKey(press('1', 'Digit1', { metaKey: true })), 'cmd+1');
  assert.equal(shortcutFromKey(press('!', 'Digit1', { metaKey: true, shiftKey: true })), 'cmd+!');
  assert.equal(shortcutFromKey(press('F5', 'F5')), 'f5');
});

test('typing and modifier keys alone are not shortcuts', () => {
  assert.equal(shortcutFromKey(press('t', 'KeyT')), null);
  assert.equal(shortcutFromKey(press('T', 'KeyT', { shiftKey: true })), null);
  assert.equal(shortcutFromKey(press('Meta', 'MetaLeft', { metaKey: true })), null);
  assert.equal(shortcutFromKey(press('Shift', 'ShiftLeft', { metaKey: true, shiftKey: true })), null);
  assert.equal(shortcutFromKey(press('ArrowLeft', 'ArrowLeft')), null);
});
