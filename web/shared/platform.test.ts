import { test } from 'node:test';
import assert from 'node:assert/strict';
import { commandKey, onWindows } from './platform.js';

test('Windows is told by navigator.platform', () => {
  assert.equal(onWindows('Win32'), true);
  assert.equal(onWindows('MacIntel'), false);
  assert.equal(onWindows(''), false);
});

test('the command key is ⌘ on macOS and Ctrl on Windows', () => {
  const cmd = { metaKey: true, ctrlKey: false };
  const ctrl = { metaKey: false, ctrlKey: true };
  assert.equal(commandKey(cmd, false), true);
  assert.equal(commandKey(ctrl, false), false);
  assert.equal(commandKey(cmd, true), false);
  assert.equal(commandKey(ctrl, true), true);
});
