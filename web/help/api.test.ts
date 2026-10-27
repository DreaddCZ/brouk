import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BridgeError } from '../shared/bridge.js';
import { parseState } from './api.js';

test('the state from the browser is checked', () => {
  const state = parseState({
    shortcuts: [{ id: 'new_tab', label: 'New tab', setting: 'shortcuts.new_tab', keys: ['⌘T'] }],
    settings: [
      {
        key: 'history.keep',
        label: 'Keep history for',
        description: '',
        section: 'History',
        scope: 'global',
        type: 'choice',
        default: '90',
        choices: [{ value: '90', label: '90 days' }],
      },
    ],
  });
  assert.equal(state.shortcuts[0]?.keys[0], '⌘T');
  assert.equal(state.settings[0]?.defaultValue, '90');
  assert.throws(() => parseState({ shortcuts: [{ id: 1 }], settings: [] }), BridgeError);
  assert.throws(() => parseState({ shortcuts: [], settings: [{ scope: 'x' }] }), BridgeError);
  assert.throws(() => parseState(null), BridgeError);
});
