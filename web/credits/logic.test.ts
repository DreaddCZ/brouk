import { test } from 'node:test';
import assert from 'node:assert/strict';
import { filterComponents, parseCredits, safeHomepage, type Component } from './logic.js';

function data(extra: Record<string, unknown> = {}): Record<string, unknown> {
  return {
    texts: ['Apache text', 'MIT text'],
    brouk: { name: 'Brouk', version: '', licence: 'Apache-2.0', homepage: 'https://github.com/DreaddCZ/brouk', group: '', texts: [{ file: 'LICENSE', text: 0 }] },
    components: [
      { name: 'regex', version: '1.13.1', licence: 'MIT OR Apache-2.0', homepage: 'https://github.com/rust-lang/regex', group: 'Rust crates',
        texts: [{ file: 'LICENSE-APACHE', text: 0 }, { file: 'LICENSE-MIT', text: 1 }] },
      { name: 'CEF', version: '154', licence: 'BSD-3-Clause', homepage: 'https://github.com/chromiumembedded/cef', group: 'Libraries',
        notices: 'chromium', texts: [{ file: 'LICENSE.txt', text: 1 }] },
    ],
    ...extra,
  };
}

test('reads the generated data with the texts in place', () => {
  const credits = parseCredits(data());
  if (credits === null) throw new Error('not parsed');
  assert.equal(credits.brouk.texts[0]?.text, 'Apache text');
  assert.deepEqual(credits.components[0]?.texts.map((t) => t.text), ['Apache text', 'MIT text']);
  assert.equal(credits.components[0]?.notices, '');
  assert.equal(credits.components[1]?.notices, 'chromium');
});

test('rejects data it does not understand', () => {
  assert.equal(parseCredits(null), null);
  assert.equal(parseCredits({}), null);
  assert.equal(parseCredits(data({ texts: [1] })), null);
  const missingText = data();
  (missingText['components'] as Array<Record<string, unknown>>)[0] = {
    name: 'x', version: '1', licence: 'MIT', homepage: '', group: 'g', texts: [{ file: 'L', text: 9 }],
  };
  assert.equal(parseCredits(missingText), null);
  const strangeNotices = data();
  (strangeNotices['components'] as Array<Record<string, unknown>>)[1]!['notices'] = '../evil';
  assert.equal(parseCredits(strangeNotices), null);
});

function component(name: string, licence: string, group: string): Component {
  return { name, version: '1', licence, homepage: '', group, notices: '', texts: [] };
}

test('filters by name and licence, keeping the groups in order', () => {
  const components = [
    component('yaml-cpp', 'MIT', 'Libraries'),
    component('regex', 'MIT OR Apache-2.0', 'Rust crates'),
    component('libsodium', 'ISC', 'Libraries'),
  ];
  assert.deepEqual([...filterComponents(components, '').keys()], ['Libraries', 'Rust crates']);
  assert.deepEqual(
    [...filterComponents(components, 'mit').values()].flat().map((c) => c.name),
    ['yaml-cpp', 'regex'],
  );
  assert.deepEqual(
    [...filterComponents(components, 'REGEX apache').values()].flat().map((c) => c.name),
    ['regex'],
  );
  assert.equal(filterComponents(components, 'nothing').size, 0);
});

test('links only web addresses', () => {
  assert.equal(safeHomepage('https://sqlite.org'), 'https://sqlite.org');
  assert.equal(safeHomepage('javascript:alert(1)'), null);
  assert.equal(safeHomepage(''), null);
});
