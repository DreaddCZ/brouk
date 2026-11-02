// Writes Brouk's icons from the beetle drawn below, so every size and
// variant comes from one drawing: the SVGs in branding/, the macOS
// AppIcon.icns, the Windows app.ico and the social preview. Run by hand
// on a Mac after changing the drawing (scripts/make_icons); the results
// are committed, so builds do not need it. Needs Google Chrome (renders
// the SVGs) and iconutil (Xcode's command line tools).

import { execFileSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const ROOT = join(import.meta.dirname, '..');
const BRANDING = join(ROOT, 'branding');
const CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';

// The colours (branding/README.md).
const MINT = '#6EE7B7';
const TEAL = '#14B8A6';
const BLUE = '#3B82F6';
const NAVY = '#0B1220';
const NAVY_LIGHT = '#1B2A47';

// The beetle in its own coordinates: 600 wide, centred on x = 300; it
// covers x 91–509 and y 83–589 with the strokes. `stroke` is the width of
// the legs and antennae (thicker for the smallest sizes).
function beetle(fill, stroke = 30) {
  return `
    <g fill="none" stroke="${fill}" stroke-width="${stroke}" stroke-linecap="round" stroke-linejoin="round">
      <path d="M262 168 C250 130 228 108 196 98"/>
      <path d="M338 168 C350 130 372 108 404 98"/>
      <path d="M178 266 L132 244 L114 330"/>
      <path d="M422 266 L468 244 L486 330"/>
      <path d="M124 404 L106 422 L106 492"/>
      <path d="M476 404 L494 422 L494 492"/>
    </g>
    <g fill="${fill}">
      <path d="M200 250 C200 196 246 160 300 160 C354 160 400 196 400 250 Z"/>
      <path d="M292 266 H226 C170 266 150 322 150 395 C150 490 205 560 292 574 Z"/>
      <path d="M308 266 H374 C430 266 450 322 450 395 C450 490 395 560 308 574 Z"/>
    </g>`;
}

// Teal to blue, from the top right to the bottom left, in the beetle's
// coordinates.
const BUG_GRADIENT = `
    <linearGradient id="bug" gradientUnits="userSpaceOnUse" x1="430" y1="150" x2="180" y2="560">
      <stop offset="0" stop-color="${MINT}"/>
      <stop offset="0.42" stop-color="${TEAL}"/>
      <stop offset="0.9" stop-color="${BLUE}"/>
    </linearGradient>`;

const TILE_GRADIENT = `
    <linearGradient id="tile" x1="0.2" y1="0" x2="0.8" y2="1">
      <stop offset="0" stop-color="${NAVY_LIGHT}"/>
      <stop offset="1" stop-color="${NAVY}"/>
    </linearGradient>`;

// The beetle on the navy tile, 1024 × 1024. `tile` is the tile's offset
// from the edge, `scale` the beetle's size on it.
function tileIcon({ tile, radius, scale, stroke }) {
  const x = 512 - 300 * scale;
  const y = 512 - 336 * scale;
  const size = 1024 - 2 * tile;
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024" width="1024" height="1024">
  <defs>${TILE_GRADIENT}${BUG_GRADIENT}
  </defs>
  <rect x="${tile}" y="${tile}" width="${size}" height="${size}" rx="${radius}" fill="url(#tile)"/>
  <g transform="translate(${x} ${y}) scale(${scale})">${beetle('url(#bug)', stroke)}
  </g>
</svg>
`;
}

// The beetle alone on a transparent square.
function symbol(fill) {
  const defs = fill === 'url(#bug)' ? `\n  <defs>${BUG_GRADIENT}\n  </defs>` : '';
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="49 85 502 502" width="1024" height="1024">${defs}${beetle(fill)}
</svg>
`;
}

const svgs = {
  // macOS: Apple's grid, a 824 tile with a 100 margin for the shadow.
  'icon-macos.svg': tileIcon({ tile: 100, radius: 185, scale: 1.18 }),
  // Windows: the tile fills the square, as on Windows' own icons.
  'icon-windows.svg': tileIcon({ tile: 24, radius: 190, scale: 1.4 }),
  // Windows at 32 px and less: a bigger beetle with thicker legs.
  'icon-windows-small.svg': tileIcon({ tile: 0, radius: 180, scale: 1.62, stroke: 40 }),
  'symbol.svg': symbol('url(#bug)'),
  'symbol-black.svg': symbol('#000000'),
  'symbol-white.svg': symbol('#FFFFFF'),
  // Layers for Apple's Icon Composer (macOS 26): the background and the
  // beetle as the foreground.
  'icon-layer-background.svg': `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024" width="1024" height="1024">
  <defs>${TILE_GRADIENT}
  </defs>
  <rect width="1024" height="1024" fill="url(#tile)"/>
</svg>
`,
};

mkdirSync(BRANDING, { recursive: true });
for (const [name, svg] of Object.entries(svgs)) {
  writeFileSync(join(BRANDING, name), svg);
}

const work = mkdtempSync(join(tmpdir(), 'brouk-icons-'));

// Renders a page of `width` × `height` pixels to a PNG with transparency.
function screenshot(html, width, height, out) {
  const page = join(work, 'page.html');
  writeFileSync(page, html);
  execFileSync(CHROME, [
    '--headless', '--disable-gpu', '--hide-scrollbars',
    '--default-background-color=00000000', '--force-device-scale-factor=1',
    `--window-size=${width},${height}`, `--screenshot=${out}`,
    `--user-data-dir=${join(work, 'profile')}`, `file://${page}`,
  ], { stdio: 'ignore' });
}

// Renders an SVG of branding/ at `size` × `size` pixels.
function render(name, size, out) {
  screenshot(
    `<!doctype html><style>html,body{margin:0;background:transparent}` +
      `img{display:block;width:${size}px;height:${size}px}</style>` +
      `<img src="file://${join(BRANDING, name)}">`,
    size, size, out);
  return readFileSync(out);
}

// macOS: an iconset of every size Apple lists, turned into an .icns.
const iconset = join(work, 'AppIcon.iconset');
mkdirSync(iconset);
for (const size of [16, 32, 128, 256, 512]) {
  render('icon-macos.svg', size, join(iconset, `icon_${size}x${size}.png`));
  render('icon-macos.svg', size * 2, join(iconset, `icon_${size}x${size}@2x.png`));
}
execFileSync('iconutil', ['-c', 'icns', iconset, '-o',
  join(ROOT, 'src/platform/mac/AppIcon.icns')]);

// Windows: an .ico with PNG images (Windows Vista and later read them),
// the sizes Windows uses at 100–200 % display scaling.
const images = [16, 20, 24, 32, 40, 48, 64, 256].map((size) => ({
  size,
  png: render(size <= 32 ? 'icon-windows-small.svg' : 'icon-windows.svg', size,
    join(work, `ico-${size}.png`)),
}));
const header = Buffer.alloc(6);
header.writeUInt16LE(0, 0);  // reserved
header.writeUInt16LE(1, 2);  // type: icon
header.writeUInt16LE(images.length, 4);
const entries = [];
let offset = 6 + 16 * images.length;
for (const { size, png } of images) {
  const entry = Buffer.alloc(16);
  entry.writeUInt8(size === 256 ? 0 : size, 0);  // width (0 means 256)
  entry.writeUInt8(size === 256 ? 0 : size, 1);  // height
  entry.writeUInt16LE(1, 4);   // colour planes
  entry.writeUInt16LE(32, 6);  // bits per pixel
  entry.writeUInt32LE(png.length, 8);
  entry.writeUInt32LE(offset, 12);
  offset += png.length;
  entries.push(entry);
}
writeFileSync(join(ROOT, 'src/platform/win/app.ico'),
  Buffer.concat([header, ...entries, ...images.map((i) => i.png)]));

// The repository's social preview (GitHub → Settings → Social preview).
screenshot(
  `<!doctype html><style>
    html,body{margin:0;width:1280px;height:640px}
    body{display:flex;align-items:center;justify-content:center;gap:40px;
      background:linear-gradient(160deg,${NAVY_LIGHT},${NAVY})}
    img{width:200px;height:200px}
    span{font:700 150px -apple-system,"Helvetica Neue",sans-serif;color:#fff;
      letter-spacing:-2px}
  </style><img src="file://${join(BRANDING, 'symbol.svg')}"><span>Brouk</span>`,
  1280, 640, join(BRANDING, 'social-preview.png'));

rmSync(work, { recursive: true, force: true });
console.log('Icons written to branding/, src/platform/mac and src/platform/win.');
