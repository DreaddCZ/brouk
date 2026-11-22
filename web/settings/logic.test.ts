import { test } from 'node:test';
import assert from 'node:assert/strict';
import type { SettingItem } from './api.js';
import { isShown, parseSettingTarget, sections, shortcutFromKey, topics, type KeyPress } from './logic.js';

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

test('topics put global and per-profile settings of a subject together', () => {
  const result = topics(
    [item('a', 'Search'), item('b', 'Ad blocking'), item('c', 'Profiles'), item('d', 'Search')],
    [item('e', 'Ad blocking'), item('f', 'Profile'), item('g', 'Downloads')],
  );
  assert.deepEqual(
    result.map((t) => [t.title, t.global.map((i) => i.key), t.profile.map((i) => i.key)]),
    [
      ['Search', ['a', 'd'], []],
      ['Ad blocking', ['b'], ['e']],
      ['Profiles', ['c'], ['f']],
      ['Downloads', [], ['g']],
    ],
  );
});

test('topics leave out hidden settings', () => {
  const result = topics([item('search.engine', 'Search', 'google'), item('search.custom_template', 'Search')], []);
  assert.deepEqual(result[0]?.global.map((i) => i.key), ['search.engine']);
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

const shortcutFromKeyMac = (key: KeyPress): string | null => shortcutFromKey(key, false);

test('key presses become shortcuts as the settings file writes them', () => {
  assert.equal(shortcutFromKeyMac(press('t', 'KeyT', { metaKey: true })), 'cmd+t');
  assert.equal(shortcutFromKeyMac(press('T', 'KeyT', { metaKey: true, shiftKey: true })), 'shift+cmd+t');
  // Option types "ˆ"; the key is still I.
  assert.equal(shortcutFromKeyMac(press('ˆ', 'KeyI', { metaKey: true, altKey: true })), 'opt+cmd+i');
  assert.equal(shortcutFromKeyMac(press('Tab', 'Tab', { ctrlKey: true, shiftKey: true })), 'ctrl+shift+tab');
  assert.equal(shortcutFromKeyMac(press('ArrowRight', 'ArrowRight', { metaKey: true, altKey: true })), 'opt+cmd+right');
  assert.equal(shortcutFromKeyMac(press('}', 'BracketRight', { metaKey: true, shiftKey: true })), 'cmd+}');
  assert.equal(shortcutFromKeyMac(press('“', 'BracketLeft', { metaKey: true, altKey: true })), 'opt+cmd+[');
  assert.equal(shortcutFromKeyMac(press(',', 'Comma', { metaKey: true })), 'cmd+comma');
  assert.equal(shortcutFromKeyMac(press('+', 'Equal', { metaKey: true, shiftKey: true })), 'cmd+plus');
  assert.equal(shortcutFromKeyMac(press('1', 'Digit1', { metaKey: true })), 'cmd+1');
  assert.equal(shortcutFromKeyMac(press('!', 'Digit1', { metaKey: true, shiftKey: true })), 'cmd+!');
  assert.equal(shortcutFromKeyMac(press('F5', 'F5')), 'f5');
});

test('on Windows, shortcuts use Ctrl or Alt and the keys Windows names', () => {
  const win = (key: string, code: string, mods: Partial<KeyPress> = {}): string | null =>
    shortcutFromKey(press(key, code, mods), true);
  assert.equal(win('t', 'KeyT', { ctrlKey: true }), 'ctrl+t');
  assert.equal(win('T', 'KeyT', { ctrlKey: true, shiftKey: true }), 'ctrl+shift+t');
  assert.equal(win('d', 'KeyD', { altKey: true }), 'alt+d');
  assert.equal(win('ArrowLeft', 'ArrowLeft', { altKey: true }), 'alt+left');
  assert.equal(win('Delete', 'Delete', { ctrlKey: true, shiftKey: true }), 'ctrl+shift+delete');
  assert.equal(win('Backspace', 'Backspace', { ctrlKey: true }), null);
  assert.equal(win('Escape', 'Escape', { shiftKey: true }), 'shift+escape');
  assert.equal(win('F3', 'F3', { shiftKey: true }), 'shift+f3');
  // Digits by key, whatever the layout types (Czech: "+" and "1").
  assert.equal(win('+', 'Digit1', { ctrlKey: true }), 'ctrl+1');
  assert.equal(win('1', 'Digit1', { ctrlKey: true, shiftKey: true }), 'ctrl+shift+1');
  assert.equal(win('}', 'BracketRight', { ctrlKey: true, shiftKey: true }), 'ctrl+}');
  assert.equal(win('t', 'KeyT', { metaKey: true }), null);
  assert.equal(win('t', 'KeyT', { shiftKey: true }), null);
  assert.equal(win('Escape', 'Escape'), 'escape');
  // macOS: ⌫ is "delete", the forward delete key is not used.
  assert.equal(shortcutFromKey(press('Backspace', 'Backspace', { metaKey: true }), false), 'cmd+delete');
  assert.equal(shortcutFromKey(press('Delete', 'Delete', { metaKey: true }), false), null);
});

test('typing and modifier keys alone are not shortcuts', () => {
  assert.equal(shortcutFromKeyMac(press('t', 'KeyT')), null);
  assert.equal(shortcutFromKeyMac(press('T', 'KeyT', { shiftKey: true })), null);
  assert.equal(shortcutFromKeyMac(press('Meta', 'MetaLeft', { metaKey: true })), null);
  assert.equal(shortcutFromKeyMac(press('Shift', 'ShiftLeft', { metaKey: true, shiftKey: true })), null);
  assert.equal(shortcutFromKeyMac(press('ArrowLeft', 'ArrowLeft')), null);
});

test('the page opens at the setting in the address fragment', () => {
  assert.deepEqual(parseSettingTarget('#global:search.engine'), { profile: null, key: 'search.engine' });
  assert.deepEqual(parseSettingTarget('#p-work:profile.color'), { profile: 'p-work', key: 'profile.color' });
  assert.equal(parseSettingTarget(''), null);
  assert.equal(parseSettingTarget('#global'), null);
  assert.equal(parseSettingTarget('#global:<script>'), null);
});
