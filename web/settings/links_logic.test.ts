import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  completeRules,
  completeWrappers,
  moveProfileUp,
  problemsByRow,
  profileName,
  profilesToAdd,
  rowsFromWrappers,
} from './links_logic.js';
import { parseLinksState, parseSaveProblems } from './links_api.js';

const profiles = [
  { id: 'default', name: 'Main', color: 'grey' },
  { id: 'p-a', name: 'Work', color: 'blue' },
];

test('only complete rows are saved, and problems find their rows', () => {
  const rules = completeRules([
    { pattern: ' example.com ', profiles: ['p-a'] },
    { pattern: '', profiles: ['p-a'] },
    { pattern: 'portal.azure.com', profiles: [] },
    { pattern: 'bad pattern', profiles: ['default'] },
  ]);
  assert.deepEqual(rules.entries, [
    { pattern: 'example.com', profiles: ['p-a'] },
    { pattern: 'bad pattern', profiles: ['default'] },
  ]);
  assert.deepEqual(rules.rows, [0, 3]);

  const wrappers = completeWrappers([
    { pattern: 'go.example', opaque: true, parameter: 'ignored' },
    { pattern: 'r.example', opaque: false, parameter: '' },
    { pattern: 'x.example', opaque: false, parameter: ' u ' },
  ]);
  assert.deepEqual(wrappers.entries, [
    { pattern: 'go.example', parameter: '' },
    { pattern: 'x.example', parameter: 'u' },
  ]);

  const byRow = problemsByRow(
    [
      { kind: 'rule', index: 1, message: 'not a pattern' },
      { kind: 'wrapper', index: 1, message: 'bad parameter' },
    ],
    rules,
    wrappers,
  );
  assert.equal(byRow.get('rule:3'), 'not a pattern');
  assert.equal(byRow.get('wrapper:2'), 'bad parameter');
});

test('wrapper rows know opaque wrappers', () => {
  assert.deepEqual(rowsFromWrappers([{ pattern: 'a', parameter: '' }, { pattern: 'b', parameter: 'u' }]), [
    { pattern: 'a', opaque: true, parameter: '' },
    { pattern: 'b', opaque: false, parameter: 'u' },
  ]);
});

test('profiles of a rule', () => {
  assert.equal(profileName('p-a', profiles), 'Work');
  assert.equal(profileName('p-gone', profiles), 'Deleted profile');
  assert.deepEqual(profilesToAdd({ pattern: '', profiles: ['p-a'] }, profiles).map((p) => p.id), ['default']);
  const row = { pattern: '', profiles: ['a', 'b', 'c'] };
  moveProfileUp(row, 2);
  assert.deepEqual(row.profiles, ['a', 'c', 'b']);
  moveProfileUp(row, 0);
  assert.deepEqual(row.profiles, ['a', 'c', 'b']);
});

test('answers are checked', () => {
  const state = parseLinksState({
    ready: true,
    rules: [{ pattern: 'example.com', profiles: ['p-a'] }],
    wrappers: [{ pattern: 'go.example', parameter: '' }],
    builtins: [{ name: 'Mimecast', hosts: 'protect-*.mimecast.com', opaque: true }],
    profiles,
    problems: [],
    isDefaultBrowser: false,
  });
  assert.equal(state.rules[0]?.profiles[0], 'p-a');
  assert.equal(state.builtins[0]?.opaque, true);
  assert.throws(() => parseLinksState({ rules: 'x' }));
  assert.throws(() => parseSaveProblems({ problems: [{ kind: 'other', index: 0, message: '' }] }));
  assert.deepEqual(parseSaveProblems({ problems: [] }), []);
});
