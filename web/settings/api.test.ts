import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BridgeError } from '../shared/bridge.js';
import { SettingsApi, parseItem, parseState } from './api.js';

const theme = {
  key: 'appearance.theme',
  type: 'choice',
  format: 'text',
  section: 'Appearance',
  label: 'Theme',
  description: '',
  restart: false,
  choices: [{ value: 'system', label: 'Same as macOS' }],
  value: 'system',
};
const spelling = { ...theme, key: 'content.spellcheck', type: 'bool', choices: [], value: true };

test('parseItem checks types and values', () => {
  assert.equal(parseItem(theme).choices[0]?.label, 'Same as macOS');
  assert.equal(parseItem(spelling).value, true);
  assert.throws(() => parseItem({ ...theme, type: 'number' }), BridgeError);
  assert.throws(() => parseItem({ ...spelling, value: 'yes' }), BridgeError);
  assert.throws(() => parseItem({ ...theme, value: false }), BridgeError);
  assert.throws(() => parseItem({ ...theme, format: 'html' }), BridgeError);
});

test('parseState', () => {
  const state = parseState({
    global: [theme, spelling],
    profiles: [{ id: 'default', name: 'Main' }],
    profile: { id: 'default', items: [] },
    problems: ['settings.yaml: line 2, x: unknown setting'],
  });
  assert.equal(state.global.length, 2);
  assert.equal(state.profile.id, 'default');
  assert.equal(state.problems.length, 1);
  assert.throws(() => parseState({ ...state, problems: [1] }), BridgeError);
});

test('set sends the scope and passes on errors', async () => {
  const sent: unknown[] = [];
  const api = new SettingsApi(async (request) => {
    sent.push(JSON.parse(request));
    return '{"restart":false}';
  });
  assert.equal(await api.set(null, 'search.engine', 'bing'), false);
  await api.set('p-1', 'profile.color', 'red');
  assert.deepEqual(sent, [
    { scope: 'global', key: 'search.engine', value: 'bing', type: 'settings.set' },
    { scope: 'profile', profile: 'p-1', key: 'profile.color', value: 'red', type: 'settings.set' },
  ]);
  const failing = new SettingsApi(async () => {
    throw new BridgeError('must be one of system, light, dark');
  });
  await assert.rejects(failing.set(null, 'appearance.theme', 'pink'), /must be one of/);
});
