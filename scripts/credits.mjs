// The open-source licences page (spec 023): collects the licence texts of
// everything third-party that ships in the app, at build time.
//
//   node scripts/credits.mjs <out-dir> --cef <dir> --deps <dir>
//        --cargo <cargo> --cargo-home <dir> --rust-target <triple>
//        [--sparkle <dir>]
//
// Writes <out-dir>/data.js (the page's data: Brouk's licence, then every
// component with its licence texts, identical texts stored once),
// <out-dir>/chromium.html (CEF's CREDITS.html, which lists Chromium's
// components, with our stylesheet) and <out-dir>/rust-std.html (the Rust
// standard library's notices, from the pinned toolchain). Fails when a shipped component has no
// licence text, or when third_party/README.md lists a component this
// script does not know: adding one to the app means adding its licence here.
//
// Pure functions are exported for scripts/credits.test.mjs.
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const ROOT = path.resolve(import.meta.dirname, '..');

/** Licence files as projects name them (LICENSE-MIT, COPYING, NOTICE…). */
export function licenceFileNames(names) {
  return names
    .filter((name) => /^(licen[cs]e|copying|notice|unlicense|third_party_licenses)/i.test(name))
    .sort();
}

/**
 * The rows of the table in third_party/README.md: [{name, version, licence}].
 * Names lose their Markdown (`code`) and anything in parentheses.
 */
export function readmeComponents(markdown) {
  const rows = [];
  for (const line of markdown.split('\n')) {
    if (!line.startsWith('|')) continue;
    const cells = line.split('|').map((c) => c.trim());
    if (cells.length < 5 || cells[1] === 'Name' || /^-+$/.test(cells[1] ?? '')) continue;
    rows.push({ name: cells[1], version: cells[2], licence: cells[3] });
  }
  return rows;
}

/**
 * The crates linked into the app, from `cargo metadata` (with
 * --filter-platform): everything the root reaches through normal
 * dependencies, without the root (our own wrapper) and without procedural
 * macros, which run in the compiler and are not in the app (nor is what only
 * they use).
 */
export function rustPackages(metadata) {
  const packages = new Map(metadata.packages.map((p) => [p.id, p]));
  const nodes = new Map(metadata.resolve.nodes.map((n) => [n.id, n]));
  const isProcMacro = (p) => p.targets.some((t) => t.kind.includes('proc-macro'));
  const seen = new Set();
  const queue = [metadata.resolve.root];
  while (queue.length > 0) {
    const id = queue.pop();
    if (seen.has(id)) continue;
    seen.add(id);
    for (const dep of nodes.get(id)?.deps ?? []) {
      const normal = dep.dep_kinds.some((k) => k.kind === null);
      const target = packages.get(dep.pkg);
      if (normal && target && !isProcMacro(target)) queue.push(dep.pkg);
    }
  }
  seen.delete(metadata.resolve.root);
  return [...seen]
    .map((id) => packages.get(id))
    .sort((a, b) => a.name.localeCompare(b.name) || a.version.localeCompare(b.version));
}

/**
 * npm packages reached from `roots` through their dependencies:
 * readPackage(name) gives the package.json of node_modules/<name>.
 */
export function npmPackages(roots, readPackage) {
  const seen = new Map();
  const queue = [...roots];
  while (queue.length > 0) {
    const name = queue.pop();
    if (seen.has(name)) continue;
    const pkg = readPackage(name);
    seen.set(name, pkg);
    queue.push(...Object.keys(pkg.dependencies ?? {}));
  }
  return [...seen.values()].sort((a, b) => a.name.localeCompare(b.name));
}

/** The MIT licence text with its copyright holders. */
export function mitText(holders) {
  return `Copyright (c) ${holders}

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
`;
}

/** SQLite's public-domain dedication, from the top of sqlite3.h. */
export function sqliteBlessing(header) {
  const match = /\*\* The author disclaims copyright[\s\S]*?(?=\n\*{5,})/.exec(header);
  if (!match) return null;
  return match[0]
    .split('\n')
    .map((line) => line.replace(/^\*\* ?/, ''))
    .join('\n')
    .trim();
}

/**
 * The page's data: components with their texts as indices into `texts`, so
 * that the same licence (the Apache licence of many crates) is stored once.
 */
export function pageData(brouk, components) {
  const texts = [];
  const index = new Map();
  const ref = (text) => {
    if (!index.has(text)) {
      index.set(text, texts.length);
      texts.push(text);
    }
    return index.get(text);
  };
  const entry = (c) => ({
    name: c.name,
    version: c.version,
    licence: c.licence,
    homepage: c.homepage,
    group: c.group,
    // A page of further notices next to this one (chromium, rust-std).
    ...(c.notices ? { notices: c.notices } : {}),
    texts: c.texts.map((t) => ({ file: t.file, text: ref(t.text) })),
  });
  return { brouk: entry(brouk), components: components.map(entry), texts };
}

/**
 * Chromium's credits page from CEF, with our stylesheet in place of the
 * chrome:// ones it links (they do not exist at brouk://).
 */
export function chromiumCredits(html) {
  const links = /<link rel="stylesheet" href="chrome:\/\/[^"]*">\n?/g;
  if (!links.test(html)) return null;
  let first = true;
  return html.replace(links, () => {
    const replacement = first ? '<link rel="stylesheet" href="chromium.css">\n' : '';
    first = false;
    return replacement;
  });
}

// ---- Collecting (reads the build's files) ----

function readTexts(dir, names = licenceFileNames(fs.readdirSync(dir))) {
  return names.map((file) => ({ file, text: fs.readFileSync(path.join(dir, file), 'utf8') }));
}

function homepageOf(pkg) {
  const repo = typeof pkg.repository === 'string' ? pkg.repository : pkg.repository?.url;
  return (pkg.homepage || repo || '').replace(/^git\+/, '').replace(/\.git$/, '').replace(/#.*$/, '');
}

// Crates published without licence files: the standard text of their
// licence. Checked by hand; a new crate without files fails the build
// until it is added here.
const RUST_WITHOUT_FILES = {
  // MIT, copyright of its authors as listed in Cargo.toml.
  seahash: (pkg) => mitText(pkg.authors.join(', ')),
  // MPL-2.0 and Apache-2.0 need no copyright line, only their text.
  selectors: (_pkg, standard) => standard['MPL-2.0'],
  flatbuffers: (_pkg, standard) => standard['Apache-2.0'],
};

// Components of third_party/README.md: how each one's licence is found.
// `shipped: false` for what does not go into the app.
const README_SOURCES = [
  {
    match: /^Chromium Embedded Framework/,
    name: 'Chromium Embedded Framework (CEF)',
    homepage: 'https://github.com/chromiumembedded/cef',
    notices: 'chromium',
    texts: (o) => readTexts(o.cef, ['LICENSE.txt']),
  },
  {
    match: /^yaml-cpp/,
    homepage: 'https://github.com/jbeder/yaml-cpp',
    texts: (o) => readTexts(path.join(o.deps, 'yaml-cpp-src')),
  },
  {
    match: /^SQLite/,
    name: 'SQLite',
    homepage: 'https://sqlite.org',
    texts: (o) => {
      const blessing = sqliteBlessing(
        fs.readFileSync(path.join(o.deps, 'sqlite-src', 'sqlite3.h'), 'utf8'));
      return blessing ? [{ file: 'sqlite3.h', text: blessing }] : [];
    },
  },
  {
    match: /^libsodium/,
    homepage: 'https://libsodium.org',
    texts: (o) => readTexts(path.join(o.deps, 'sodium-src')),
  },
  {
    match: /^Brave adblock-resources/,
    name: 'Brave adblock-resources',
    homepage: 'https://github.com/brave/adblock-resources',
    // MPL-2.0; its text as in the adblock crate.
    texts: (o) => [{ file: 'LICENSE', text: o.standard['MPL-2.0'] }],
  },
  {
    match: /^Sparkle/,
    homepage: 'https://sparkle-project.org',
    macOnly: true,
    texts: (o) => readTexts(o.sparkle, ['LICENSE']),
  },
  // Listed crate by crate from Cargo's metadata.
  { match: /^adblock-rust/, covered: 'rust' },
  // Only in the tests.
  { match: /^GoogleTest/, shipped: false },
];

// Packages the pages use at run time (scripts/web copies them into the
// passwords page), with what they depend on.
const NPM_ROOTS = ['@zxcvbn-ts/core', '@zxcvbn-ts/language-common', '@zxcvbn-ts/language-en'];

function collect(options) {
  const metadata = JSON.parse(execFileSync(
    options.cargo,
    ['metadata', '--offline', '--locked', '--format-version', '1',
      '--filter-platform', options.rustTarget,
      '--manifest-path', path.join(ROOT, 'src/adblock/Cargo.toml')],
    { env: { ...process.env, CARGO_HOME: options.cargoHome }, maxBuffer: 64 * 1024 * 1024 },
  ));
  const crates = rustPackages(metadata);
  const adblock = crates.find((c) => c.name === 'adblock');
  if (!adblock) throw new Error('the adblock crate is not in the dependency tree');
  const standard = {
    'Apache-2.0': fs.readFileSync(path.join(ROOT, 'LICENSE'), 'utf8'),
    'MPL-2.0': fs.readFileSync(path.join(path.dirname(adblock.manifest_path), 'LICENSE'), 'utf8'),
  };
  const collectOptions = { ...options, standard };
  const problems = [];
  const components = [];

  for (const row of readmeComponents(fs.readFileSync(path.join(ROOT, 'third_party/README.md'), 'utf8'))) {
    const source = README_SOURCES.find((s) => s.match.test(row.name));
    if (!source) {
      problems.push(`${row.name}: listed in third_party/README.md, but scripts/credits.mjs does not know its licence`);
      continue;
    }
    if (source.shipped === false || source.covered || (source.macOnly && !options.sparkle)) continue;
    const texts = source.texts(collectOptions);
    if (texts.length === 0) problems.push(`${row.name}: no licence text found`);
    components.push({
      name: source.name ?? row.name.replace(/\s*\(.*$/, ''),
      ...(source.notices ? { notices: source.notices } : {}),
      version: row.version,
      licence: row.licence.replace(/\s*\(.*$/, ''),
      homepage: source.homepage,
      group: 'Libraries',
      texts,
    });
  }

  // The Rust standard library is compiled into the ad blocking engine.
  const rustVersion = /rust-([\d.]+)/.exec(options.rustDoc)?.[1] ?? '';
  const rustLicences = path.join(options.rustDoc, 'licenses');
  components.push({
    name: 'Rust standard library',
    version: rustVersion,
    licence: 'MIT OR Apache-2.0',
    homepage: 'https://www.rust-lang.org',
    group: 'Rust crates (ad blocking engine)',
    notices: 'rust-std',
    texts: readTexts(rustLicences, ['MIT.txt', 'Apache-2.0.txt']),
  });

  for (const crate of crates) {
    const dir = path.dirname(crate.manifest_path);
    let texts = readTexts(dir);
    const fallback = RUST_WITHOUT_FILES[crate.name];
    if (texts.length === 0 && fallback) {
      texts = [{ file: `${crate.license} (standard text)`, text: fallback(crate, standard) }];
    }
    if (texts.length === 0 || texts.some((t) => !t.text)) {
      problems.push(`Rust crate ${crate.name} ${crate.version}: no licence file`);
    }
    components.push({
      name: crate.name,
      version: crate.version,
      licence: crate.license ?? '',
      homepage: homepageOf(crate) || `https://crates.io/crates/${crate.name}`,
      group: 'Rust crates (ad blocking engine)',
      texts,
    });
  }

  const modules = path.join(ROOT, 'web/node_modules');
  const readPackage = (name) => JSON.parse(fs.readFileSync(path.join(modules, name, 'package.json'), 'utf8'));
  for (const pkg of npmPackages(NPM_ROOTS, readPackage)) {
    const texts = readTexts(path.join(modules, pkg.name));
    if (texts.length === 0) problems.push(`npm package ${pkg.name}: no licence file`);
    components.push({
      name: pkg.name,
      version: pkg.version,
      licence: pkg.license ?? '',
      homepage: homepageOf(pkg) || `https://www.npmjs.com/package/${pkg.name}`,
      group: 'JavaScript packages (password strength)',
      texts,
    });
  }

  const brouk = {
    name: 'Brouk',
    version: '',
    licence: 'Apache-2.0',
    homepage: 'https://github.com/DreaddCZ/brouk',
    group: '',
    texts: readTexts(ROOT, ['NOTICE', 'LICENSE']),
  };
  return { brouk, components, problems };
}

function parseArgs(argv) {
  const [outDir, ...rest] = argv;
  const options = { outDir };
  for (let i = 0; i < rest.length; i += 2) {
    const key = rest[i]?.replace(/^--/, '').replace(/-(\w)/g, (_, c) => c.toUpperCase());
    options[key] = rest[i + 1];
  }
  // The toolchain's documentation folder, next to the real cargo.
  if (options.cargo) {
    options.rustDoc = path.resolve(path.dirname(fs.realpathSync(options.cargo)), '../share/doc/rust');
  }
  for (const required of ['outDir', 'cef', 'deps', 'cargo', 'cargoHome', 'rustTarget']) {
    if (!options[required]) throw new Error(`missing ${required}; see scripts/credits.mjs`);
  }
  return options;
}

function main(argv) {
  const options = parseArgs(argv);
  const { brouk, components, problems } = collect(options);
  const chromium = chromiumCredits(fs.readFileSync(path.join(options.cef, 'CREDITS.html'), 'utf8'));
  if (!chromium) problems.push("CEF's CREDITS.html: stylesheet links not found");
  if (problems.length > 0) {
    for (const problem of problems) console.error(`credits: ${problem}`);
    return 1;
  }
  fs.mkdirSync(options.outDir, { recursive: true });
  const data = pageData(brouk, components);
  fs.writeFileSync(
    path.join(options.outDir, 'data.js'),
    `// Generated by scripts/credits.mjs; do not edit.\nexport const credits = ${JSON.stringify(data)};\n`,
  );
  fs.writeFileSync(path.join(options.outDir, 'chromium.html'), chromium);
  fs.copyFileSync(path.join(options.rustDoc, 'COPYRIGHT-library.html'),
    path.join(options.outDir, 'rust-std.html'));
  return 0;
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  try {
    process.exitCode = main(process.argv.slice(2));
  } catch (error) {
    console.error(`credits: ${error.message}`);
    process.exitCode = 1;
  }
}
