// Writes Brouk's privacy policy (store/windows/privacy.md, 032) as one HTML
// page for the releases repository's GitHub Pages, which serve files as
// they are (no Jekyll). Published at
// https://dreaddcz.github.io/brouk-releases/privacy (the Store listing links
// it). Run after changing the policy, then commit the page there:
//
//   node scripts/privacy_page.mjs <releases repo>/privacy.html

import { readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { createRequire } from 'node:module';

const ROOT = join(import.meta.dirname, '..');
// markdown-it as pinned for help (016), from web/package-lock.json.
const require = createRequire(join(ROOT, 'web', 'package.json'));
const MarkdownIt = require('markdown-it');

const out = process.argv[2];
if (!out) {
  console.error('Usage: node scripts/privacy_page.mjs <out.html>');
  process.exit(2);
}
const body = new MarkdownIt({ html: false, linkify: true }).render(
  readFileSync(join(ROOT, 'store/windows/privacy.md'), 'utf8'));

writeFileSync(out, `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Brouk privacy policy</title>
<style>
  :root { color-scheme: light dark; --fg: #1b2a47; --bg: #fff; --line: #d0d7e2; }
  @media (prefers-color-scheme: dark) { :root { --fg: #e6ebf3; --bg: #0b1220; --line: #2a3a5c; } }
  body { margin: 0 auto; max-width: 760px; padding: 24px 16px 64px;
    font: 16px/1.55 -apple-system, "Segoe UI", system-ui, sans-serif;
    color: var(--fg); background: var(--bg); }
  h1 { font-size: 28px; } h2 { font-size: 20px; margin-top: 32px; }
  a { color: #14b8a6; }
  table { border-collapse: collapse; width: 100%; font-size: 14px; display: block; overflow-x: auto; }
  th, td { border: 1px solid var(--line); padding: 6px 8px; text-align: left; vertical-align: top; }
  code { font-size: 90%; }
</style>
</head>
<body>
${body}</body>
</html>
`);
console.log(`Written ${out}`);
