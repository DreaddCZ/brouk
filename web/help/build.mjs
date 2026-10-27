// Builds the help page (016) at build time:
//
//   node web/help/build.mjs <out-dir>
//
// Converts web/help/content/*.md (in file name order, one file per section)
// with markdown-it into <out-dir>/help/index.html, writes the list of
// sections for the command palette to <out-dir>/help/sections.json, and
// checks that every help link in the code and pages points to an anchor
// that exists. A missing anchor fails the build.
//
// Anchors are written in the Markdown, never derived from the wording:
//   ## Request rules {#devtools-request-rules}
import { readFileSync, readdirSync, writeFileSync, mkdirSync, statSync } from 'node:fs';
import { join, resolve, relative } from 'node:path';
import MarkdownIt from 'markdown-it';

const kHere = import.meta.dirname;
const kRoot = resolve(kHere, '../..');
const outDir = process.argv[2];
if (!outDir) {
  console.error('usage: node web/help/build.mjs <out-dir>');
  process.exit(2);
}

// ---- Markdown ----

const md = new MarkdownIt({ html: true, linkify: false, typographer: true });

// "## Title {#anchor}": the anchor becomes the heading's id.
md.core.ruler.push('explicit_anchors', (state) => {
  const tokens = state.tokens;
  for (let i = 0; i < tokens.length; i++) {
    if (tokens[i].type !== 'heading_open') continue;
    const inline = tokens[i + 1];
    const match = /\s*\{#([a-z0-9-]+)\}\s*$/.exec(inline.content);
    if (!match) continue;
    tokens[i].attrSet('id', match[1]);
    inline.content = inline.content.slice(0, match.index);
    const last = inline.children[inline.children.length - 1];
    if (last && last.type === 'text') last.content = last.content.replace(/\s*\{#[a-z0-9-]+\}\s*$/, '');
  }
});

// Links to web sites are marked, so the page can show them as such.
const defaultLinkOpen =
  md.renderer.rules.link_open ?? ((tokens, idx, options, env, self) => self.renderToken(tokens, idx, options));
md.renderer.rules.link_open = (tokens, idx, options, env, self) => {
  const href = tokens[idx].attrGet('href') ?? '';
  if (/^https?:/.test(href)) tokens[idx].attrJoin('class', 'external');
  return defaultLinkOpen(tokens, idx, options, env, self);
};

const contentDir = join(kHere, 'content');
const files = readdirSync(contentDir)
  .filter((name) => name.endsWith('.md'))
  .sort();

const problems = [];
const anchors = new Map(); // anchor -> file
const sections = [];
let body = '';
for (const name of files) {
  const source = readFileSync(join(contentDir, name), 'utf8');
  const tokens = md.parse(source, {});
  let first = true;
  for (let i = 0; i < tokens.length; i++) {
    const token = tokens[i];
    if (token.type !== 'heading_open') continue;
    const id = token.attrGet('id');
    const title = tokens[i + 1].content;
    if (!id) {
      problems.push(`${name}: heading "${title}" has no {#anchor}`);
      continue;
    }
    if (anchors.has(id)) problems.push(`${name}: anchor #${id} is also used in ${anchors.get(id)}`);
    anchors.set(id, name);
    const level = Number(token.tag.slice(1));
    if (first && level !== 2) problems.push(`${name}: the first heading must be "## Title {#anchor}"`);
    sections.push({ anchor: id, title, level, file: name });
    first = false;
  }
  body += `<section class="help-section">\n${md.renderer.render(tokens, md.options, {})}</section>\n`;
}

// Anchors made by the page itself: one per settings page section (the
// settings reference, from the settings schema).
const schema = readFileSync(join(kRoot, 'src/core/settings_schema.cc'), 'utf8');
for (const [, section] of schema.matchAll(/\.section = "([^"]+)"/g)) {
  anchors.set(`settings-${slug(section)}`, 'settings reference');
}

function slug(text) {
  return text
    .normalize('NFD')
    .replace(/[̀-ͯ]/g, '')
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '');
}

// ---- Links to help everywhere else ----

const kPatterns = [
  /helpButton\(\s*['"]([a-z0-9-]+)['"]/g, // TypeScript pages
  /openHelp\(\s*['"]([a-z0-9-]+)['"]/g,
  /data-help="([a-z0-9-]+)"/g, // static HTML
  /openHelpAt:@"([a-z0-9-]+)"/g, // native code
  /BRHelpButton\(@"([a-z0-9-]+)"/g,
  /brouk:\/\/help\/#([a-z0-9-]+)/g,
];
function walk(dir, out) {
  for (const name of readdirSync(dir)) {
    if (name === 'node_modules' || name.startsWith('.')) continue;
    const path = join(dir, name);
    if (statSync(path).isDirectory()) walk(path, out);
    else if (/\.(ts|html|mm|cc|h)$/.test(name) && !name.endsWith('.test.ts')) out.push(path);
  }
  return out;
}
const sources = [...walk(join(kRoot, 'web'), []), ...walk(join(kRoot, 'src'), [])];
let links = 0;
for (const file of sources) {
  const text = readFileSync(file, 'utf8');
  for (const pattern of kPatterns) {
    for (const [, anchor] of text.matchAll(pattern)) {
      links++;
      if (!anchors.has(anchor)) problems.push(`${relative(kRoot, file)}: help link to #${anchor}, which help does not have`);
    }
  }
}
// Links between help sections.
for (const name of files) {
  const text = readFileSync(join(contentDir, name), 'utf8');
  for (const [, anchor] of text.matchAll(/\]\(#([a-z0-9-]+)\)/g)) {
    links++;
    if (!anchors.has(anchor)) problems.push(`${name}: link to #${anchor}, which help does not have`);
  }
}

if (problems.length > 0) {
  console.error('Help page (web/help/build.mjs):');
  for (const problem of problems) console.error(`  ${problem}`);
  process.exit(1);
}

// ---- Output ----

const template = readFileSync(join(kHere, 'template.html'), 'utf8');
mkdirSync(join(outDir, 'help'), { recursive: true });
writeFileSync(join(outDir, 'help/index.html'), template.replace('<!-- CONTENT -->', body));
writeFileSync(
  join(outDir, 'help/sections.json'),
  JSON.stringify(sections.map(({ anchor, title, level }) => ({ anchor, title, level }))),
);
console.log(`Help: ${files.length} files, ${anchors.size} anchors, ${links} links checked.`);
