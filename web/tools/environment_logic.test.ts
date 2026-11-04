import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  environmentMessage, isChanged, kNoEnvironment, parseEnvironmentState, parseLanguages, validLanguages,
} from './environment_logic.js';

test('languages', () => {
  assert.deepEqual(parseLanguages(' de-DE, de ,en '), ['de-DE', 'de', 'en']);
  assert.equal(validLanguages(['de-DE', 'zh-Hant-TW', 'en']), true);
  assert.equal(validLanguages(['de_DE']), false);
  assert.equal(validLanguages([]), false);
});

test('messages round-trip through the browser state', () => {
  const env = {
    ...kNoEnvironment,
    timeZone: 'Asia/Tokyo',
    languages: ['de-DE', 'de'],
    location: { latitude: 35.6, longitude: 139.6, accuracy: 150, label: 'Tokyo' },
    userAgent: { preset: 'chrome-android' },
    colorScheme: 'dark' as const,
  };
  const message = environmentMessage(env);
  assert.deepEqual(message, {
    timeZone: 'Asia/Tokyo', languages: ['de-DE', 'de'],
    location: { latitude: 35.6, longitude: 139.6, accuracy: 150, label: 'Tokyo' },
    userAgentPreset: 'chrome-android', colorScheme: 'dark',
  });
  const state = parseEnvironmentState({ active: message, presets: [{ id: 'chrome-android', label: 'Chrome on Android' }, 7] });
  assert.deepEqual(state.active, env);
  assert.deepEqual(state.presets, [{ id: 'chrome-android', label: 'Chrome on Android' }]);
  assert.equal(isChanged(kNoEnvironment), false);
  assert.equal(isChanged(env), true);
  assert.deepEqual(parseEnvironmentState({ active: { location: { unavailable: true }, userAgent: 'x' } }).active,
    { ...kNoEnvironment, location: 'unavailable', userAgent: { custom: 'x' } });
});
