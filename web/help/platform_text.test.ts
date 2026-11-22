import { test } from 'node:test';
import assert from 'node:assert/strict';
import { applyPlatform, buildPlatform, formatKeys } from './platform_text.js';

test('key combinations are written as each platform writes them', () => {
  assert.equal(formatKeys('mod+shift+t', 'mac'), '⇧⌘T');
  assert.equal(formatKeys('mod+shift+t', 'win'), 'Ctrl+Shift+T');
  assert.equal(formatKeys('shift+mod+t', 'mac'), '⇧⌘T');
  assert.equal(formatKeys('ctrl+alt+mod+f', 'mac'), '⌃⌥⌘F');
  assert.equal(formatKeys('ctrl+mod+f', 'win'), 'Ctrl+F');
  assert.equal(formatKeys('mod+backspace', 'mac'), '⌘⌫');
  assert.equal(formatKeys('mod+backspace', 'win'), 'Ctrl+Backspace');
  assert.equal(formatKeys('shift+f3', 'win'), 'Shift+F3');
  assert.equal(formatKeys('esc', 'mac'), 'Esc');
  assert.equal(formatKeys('return', 'win'), 'Enter');
  assert.equal(formatKeys('mod+plus', 'win'), 'Ctrl+Plus');
  assert.equal(formatKeys('mod', 'mac'), '⌘');
  assert.equal(formatKeys('mod', 'win'), 'Ctrl');
  assert.equal(formatKeys('shift+mod', 'win'), 'Ctrl+Shift');
  assert.equal(formatKeys('win+l', 'win'), 'Win+L');
});

test('anything else is not a key combination', () => {
  assert.equal(formatKeys('', 'mac'), null);
  assert.equal(formatKeys('mod+', 'mac'), null);
  assert.equal(formatKeys('mod+mod+t', 'mac'), null);
  assert.equal(formatKeys('mod+t+u', 'mac'), null);
  assert.equal(formatKeys('mod+t+shift', 'mac'), null);
  assert.equal(formatKeys('mod+f13', 'mac'), null);
  assert.equal(formatKeys('hyper+t', 'mac'), null);
});

test('text for the other platform is left out', () => {
  const source = 'Open {mac}Brouk → Settings…{/mac}{win}⋮ → Settings{/win} now.';
  assert.equal(applyPlatform(source, 'mac').text, 'Open Brouk → Settings… now.');
  assert.equal(applyPlatform(source, 'win').text, 'Open ⋮ → Settings now.');
  const blocks = 'A\n\n{mac}\nOnly Mac.\n\nStill Mac.\n{/mac}\n\n{win}\nOnly Windows.\n{/win}\n\nB\n';
  assert.equal(applyPlatform(blocks, 'mac').text, 'A\n\nOnly Mac.\n\nStill Mac.\n\nB\n');
  assert.equal(applyPlatform(blocks, 'win').text, 'A\n\nOnly Windows.\n\nB\n');
});

test('keys and shortcuts become kbd elements', () => {
  const result = applyPlatform('Press {key:mod+shift+/} or {shortcut:help}.', 'win');
  assert.equal(
    result.text,
    'Press <kbd>Ctrl+Shift+/</kbd> or <kbd class="shortcut" data-shortcut="help"></kbd>.',
  );
  assert.deepEqual(result.actions, ['help']);
  assert.deepEqual(result.problems, []);
  assert.equal(applyPlatform('{key:mod+<}', 'mac').text, '<kbd>⌘&lt;</kbd>');
});

test('broken markers are reported', () => {
  for (const platform of ['mac', 'win'] as const) {
    assert.deepEqual(applyPlatform('{mac}open', platform).problems, ['unmatched {mac}']);
    assert.deepEqual(applyPlatform('x{/win}', platform).problems, ['unmatched {/win}']);
    assert.deepEqual(applyPlatform('{mac}a{win}b{/win}{/mac}', platform).problems, [
      'unmatched {win}',
      'unmatched {/win}',
    ]);
  }
  assert.deepEqual(applyPlatform('{key:mod+t+u}', 'mac').problems, ['{key:mod+t+u} is not a key combination']);
  assert.deepEqual(applyPlatform('{shortcut:New-Tab}', 'mac').problems, ['{shortcut:New-Tab} is not an action id']);
});

test('help is built for this OS unless told otherwise', () => {
  assert.equal(buildPlatform(undefined, 'win32'), 'win');
  assert.equal(buildPlatform(undefined, 'darwin'), 'mac');
  assert.equal(buildPlatform(undefined, 'linux'), 'mac');
  assert.equal(buildPlatform('win', 'darwin'), 'win');
  assert.equal(buildPlatform('other', 'win32'), 'win');
});
