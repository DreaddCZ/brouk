import { test } from 'node:test';
import assert from 'node:assert/strict';
import { fold, matchesAll, matchRanges, queryWords } from './logic.js';
import { settingsSectionAnchor, slug } from '../shared/help.js';

test('search ignores case and accents and needs every word', () => {
  assert.equal(fold('Nastavení ČEŠTINA'), 'nastaveni cestina');
  const words = queryWords('  Request   RULES ');
  assert.deepEqual(words, ['request', 'rules']);
  assert.ok(matchesAll('Request rules change headers', words));
  assert.ok(!matchesAll('Request headers', words));
  assert.ok(matchesAll('anything', []));
  assert.ok(matchesAll('Přihlášení', queryWords('prihlaseni')));
});

test('matches map back to the original text', () => {
  const text = 'Přihlášení a prihlaseni';
  const ranges = matchRanges(text, queryWords('prihlaseni'));
  assert.deepEqual(
    ranges.map(([a, b]) => text.slice(a, b)),
    ['Přihlášení', 'prihlaseni'],
  );
  assert.deepEqual(matchRanges('aaa', ['a', 'aa']), [[0, 3]]);
  assert.deepEqual(matchRanges('abc', []), []);
});

test('settings sections have stable anchors', () => {
  assert.equal(slug('Secure DNS'), 'secure-dns');
  assert.equal(settingsSectionAnchor('Ad blocking'), 'settings-ad-blocking');
  assert.equal(slug('  Výchozí  — profil '), 'vychozi-profil');
});
