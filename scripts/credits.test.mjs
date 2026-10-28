// Tests for credits.mjs: node --test scripts/*.test.mjs
import assert from 'node:assert/strict';
import test from 'node:test';

import {
  chromiumCredits, licenceFileNames, mitText, npmPackages, pageData,
  readmeComponents, rustPackages, sqliteBlessing,
} from './credits.mjs';

test('finds licence files by their usual names', () => {
  assert.deepEqual(
    licenceFileNames(['src', 'LICENSE-MIT', 'COPYING', 'README.md', 'license.txt',
      'NOTICE.md', 'UNLICENSE', 'THIRD_PARTY_LICENSES.md', 'Cargo.toml']),
    ['COPYING', 'LICENSE-MIT', 'NOTICE.md', 'THIRD_PARTY_LICENSES.md', 'UNLICENSE', 'license.txt'],
  );
});

test('reads the components of third_party/README.md', () => {
  const markdown = `# Third-party code

| Name | Version | Licence | How | Why |
|---|---|---|---|---|
| yaml-cpp | 0.8.0 | MIT | FetchContent | settings |
| adblock-rust (crate \`adblock\`) | 0.13.3 | MPL-2.0 | Cargo | ads |

Build tools | not | a | table | row
`;
  assert.deepEqual(readmeComponents(markdown), [
    { name: 'yaml-cpp', version: '0.8.0', licence: 'MIT' },
    { name: 'adblock-rust (crate `adblock`)', version: '0.13.3', licence: 'MPL-2.0' },
  ]);
});

function crate(name, kind = 'lib') {
  return { id: name, name, version: '1.0.0', targets: [{ kind: [kind] }] };
}
const normal = (pkg) => ({ pkg, dep_kinds: [{ kind: null }] });
const build = (pkg) => ({ pkg, dep_kinds: [{ kind: 'build' }] });

test('lists the crates linked into the app', () => {
  const metadata = {
    packages: [crate('root', 'staticlib'), crate('adblock'), crate('regex'),
      crate('serde_derive', 'proc-macro'), crate('syn'), crate('cc'), crate('memchr')],
    resolve: {
      root: 'root',
      nodes: [
        { id: 'root', deps: [normal('adblock'), build('cc')] },
        // A procedural macro runs in the compiler: neither it nor what only
        // it uses is in the app.
        { id: 'adblock', deps: [normal('regex'), normal('serde_derive')] },
        { id: 'serde_derive', deps: [normal('syn')] },
        { id: 'regex', deps: [normal('memchr')] },
        { id: 'cc', deps: [] },
        { id: 'syn', deps: [] },
        { id: 'memchr', deps: [] },
      ],
    },
  };
  assert.deepEqual(rustPackages(metadata).map((p) => p.name), ['adblock', 'memchr', 'regex']);
});

test('follows npm dependencies once each', () => {
  const packages = {
    a: { name: 'a', dependencies: { c: '1', b: '1' } },
    b: { name: 'b', dependencies: { c: '1' } },
    c: { name: 'c' },
  };
  assert.deepEqual(npmPackages(['a', 'b'], (name) => packages[name]).map((p) => p.name), ['a', 'b', 'c']);
});

test('the MIT text names its copyright holders', () => {
  assert.match(mitText('Ticki'), /^Copyright \(c\) Ticki\n/);
  assert.match(mitText('Ticki'), /WITHOUT WARRANTY OF ANY KIND/);
});

test("takes SQLite's dedication from its header", () => {
  const header = `/*
** 2001-09-15
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**
*************************************************************************
** This header file defines the interface`;
  assert.equal(
    sqliteBlessing(header),
    'The author disclaims copyright to this source code.  In place of\n' +
      'a legal notice, here is a blessing:\n\n   May you do good and not evil.',
  );
  assert.equal(sqliteBlessing('/* something else */'), null);
});

test('stores each licence text once', () => {
  const apache = 'Apache License 2.0';
  const data = pageData(
    { name: 'Brouk', version: '', licence: 'Apache-2.0', homepage: 'h', group: '', texts: [{ file: 'LICENSE', text: apache }] },
    [
      { name: 'x', version: '1', licence: 'MIT OR Apache-2.0', homepage: 'h', group: 'g',
        texts: [{ file: 'LICENSE-APACHE', text: apache }, { file: 'LICENSE-MIT', text: 'MIT' }] },
      { name: 'y', version: '2', licence: 'Apache-2.0', homepage: 'h', group: 'g', notices: 'chromium',
        texts: [{ file: 'LICENSE', text: apache }] },
    ],
  );
  assert.deepEqual(data.texts, [apache, 'MIT']);
  assert.deepEqual(data.components[0]?.texts, [{ file: 'LICENSE-APACHE', text: 0 }, { file: 'LICENSE-MIT', text: 1 }]);
  assert.equal(data.components[1]?.notices, 'chromium');
  assert.equal('notices' in (data.components[0] ?? {}), false);
});

test("replaces the chrome:// stylesheets of Chromium's credits with ours", () => {
  const html = `<head>
<link rel="stylesheet" href="chrome://resources/css/text_defaults.css">
<link rel="stylesheet" href="chrome://credits/credits.css">
</head>`;
  assert.equal(chromiumCredits(html), '<head>\n<link rel="stylesheet" href="chromium.css">\n</head>');
  assert.equal(chromiumCredits('<head></head>'), null);
});
