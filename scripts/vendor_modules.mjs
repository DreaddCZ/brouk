// Copies an npm ES module and everything it imports into one flat folder
// of .js files with rewritten import paths, for internal pages: they are
// served as plain file names only, without a bundler or import maps (the
// pages' CSP allows no inline scripts). Used by scripts/web for the
// password strength estimate (@zxcvbn-ts, 008).
//
//   node scripts/vendor_modules.mjs <node_modules> <out-dir> <prefix> <spec>...
//
// Each <spec> (a package name or subpath) becomes <prefix><n>.js; the
// first ones keep stable names: <prefix>-<package>.js.
import fs from 'node:fs';
import path from 'node:path';

const [modules, outDir, prefix, ...entries] = process.argv.slice(2);

function resolveBare(spec) {
  const parts = spec.split('/');
  const name = spec.startsWith('@') ? parts.slice(0, 2).join('/') : parts[0];
  const sub = '.' + spec.slice(name.length);
  const dir = path.join(modules, name);
  const pkg = JSON.parse(fs.readFileSync(path.join(dir, 'package.json'), 'utf8'));
  let target = null;
  const exp = pkg.exports?.[sub] ?? (sub === '.' ? pkg.exports?.['.'] : undefined);
  if (exp !== undefined) {
    const pick = (e) => (typeof e === 'string' ? e : pick(e.import ?? e.default));
    target = pick(exp);
  } else if (sub === '.') {
    target = pkg.module ?? pkg.main;
  }
  if (!target) throw new Error(`cannot resolve ${spec}`);
  return path.join(dir, target);
}

const names = new Map();  // absolute file -> output name
let counter = 0;
function nameOf(file) {
  if (!names.has(file)) {
    names.set(file, `${prefix}-${++counter}.js`);
    queue.push(file);
  }
  return names.get(file);
}

const queue = [];
const stable = [];
for (const spec of entries) {
  const file = resolveBare(spec);
  names.set(file, `${prefix}-${spec.replace(/^@/, '').replace(/[\/]/g, '-')}.js`);
  queue.push(file);
  stable.push(file);
}
fs.mkdirSync(outDir, { recursive: true });
const IMPORT = /(\bfrom\s*|\bimport\s*\(?\s*)(['"])([^'"]+)\2/g;
while (queue.length > 0) {
  const file = queue.shift();
  const source = fs.readFileSync(file, 'utf8')
    .replace(/\n\/\/# sourceMappingURL=.*$/m, '');
  const rewritten = source.replace(IMPORT, (match, head, quote, spec) => {
    const target = spec.startsWith('.') ? path.resolve(path.dirname(file), spec) : resolveBare(spec);
    return `${head}${quote}./${nameOf(target)}${quote}`;
  });
  fs.writeFileSync(path.join(outDir, names.get(file)), rewritten);
}
