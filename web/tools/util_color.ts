/** Colours (027): hex, rgb(), hsl() and CSS names. */

export interface Rgba {
  /** 0–255 */
  readonly r: number;
  readonly g: number;
  readonly b: number;
  /** 0–1 */
  readonly a: number;
}

function clamp(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, value));
}

/** A number or percentage of `full` (e.g. "50%" of 255). */
function amount(text: string, full: number): number | null {
  const m = /^(-?\d*\.?\d+)(%?)$/.exec(text.trim());
  if (!m) return null;
  const value = Number(m[1]);
  return m[2] === '%' ? (value / 100) * full : value;
}

function alpha(text: string | undefined): number | null {
  if (text === undefined) return 1;
  const value = amount(text, 1);
  return value === null ? null : clamp(value, 0, 1);
}

/** The arguments of "fn(a, b, c[, d])" or "fn(a b c [/ d])". */
function args(body: string): string[] | null {
  const slash = body.split('/');
  if (slash.length > 2) return null;
  const main = (slash[0] ?? '').trim().split(/\s*,\s*|\s+/).filter((s) => s !== '');
  if (slash.length === 2) main.push((slash[1] ?? '').trim());
  return main.length === 3 || main.length === 4 ? main : null;
}

export function hslToRgb(h: number, s: number, l: number): [number, number, number] {
  const hue = ((h % 360) + 360) % 360;
  const c = (1 - Math.abs(2 * l - 1)) * s;
  const x = c * (1 - Math.abs(((hue / 60) % 2) - 1));
  const m = l - c / 2;
  const [r, g, b] = hue < 60 ? [c, x, 0] : hue < 120 ? [x, c, 0] : hue < 180 ? [0, c, x]
    : hue < 240 ? [0, x, c] : hue < 300 ? [x, 0, c] : [c, 0, x];
  return [(r + m) * 255, (g + m) * 255, (b + m) * 255];
}

export function rgbToHsl(r: number, g: number, b: number): [number, number, number] {
  const [rr, gg, bb] = [r / 255, g / 255, b / 255];
  const max = Math.max(rr, gg, bb);
  const min = Math.min(rr, gg, bb);
  const l = (max + min) / 2;
  if (max === min) return [0, 0, l];
  const d = max - min;
  const s = d / (1 - Math.abs(2 * l - 1));
  const h = max === rr ? ((gg - bb) / d) % 6 : max === gg ? (bb - rr) / d + 2 : (rr - gg) / d + 4;
  return [((h * 60) + 360) % 360, s, l];
}

/**
 * A colour from its CSS text, or null. `named` resolves CSS colour names
 * (the page asks the browser) to hex.
 */
export function parseColor(text: string, named: (name: string) => string | null): Rgba | null {
  const t = text.trim().toLowerCase();
  const hex = /^#?([0-9a-f]{3,4}|[0-9a-f]{6}|[0-9a-f]{8})$/.exec(t);
  if (hex) {
    let digits = hex[1] ?? '';
    if (digits.length <= 4) digits = [...digits].map((c) => c + c).join('');
    const byte = (i: number): number => parseInt(digits.slice(i * 2, i * 2 + 2), 16);
    return { r: byte(0), g: byte(1), b: byte(2), a: digits.length === 8 ? byte(3) / 255 : 1 };
  }
  const fn = /^(rgba?|hsla?)\((.*)\)$/.exec(t);
  if (fn) {
    const list = args(fn[2] ?? '');
    if (list === null) return null;
    const a = alpha(list[3]);
    if (a === null) return null;
    if ((fn[1] ?? '').startsWith('rgb')) {
      const [r, g, b] = list.slice(0, 3).map((v) => amount(v, 255));
      if (r == null || g == null || b == null) return null;
      return { r: clamp(r, 0, 255), g: clamp(g, 0, 255), b: clamp(b, 0, 255), a };
    }
    const h = amount((list[0] ?? '').replace(/deg$/, ''), 360);
    const s = amount(list[1] ?? '', 1);
    const l = amount(list[2] ?? '', 1);
    if (h === null || s === null || l === null || !(list[1] ?? '').endsWith('%') || !(list[2] ?? '').endsWith('%')) return null;
    const [r, g, b] = hslToRgb(h, clamp(s, 0, 1), clamp(l, 0, 1));
    return { r, g, b, a };
  }
  if (/^[a-z]+$/.test(t)) {
    const resolved = named(t);
    return resolved === null ? null : parseColor(resolved, () => null);
  }
  return null;
}

function round(value: number, digits = 0): number {
  const f = 10 ** digits;
  return Math.round(value * f) / f;
}

export interface ColorForms {
  readonly hex: string;
  readonly rgb: string;
  readonly hsl: string;
}

/** The colour in each form; alpha only where it is not 1. */
export function colorForms(c: Rgba): ColorForms {
  const [r, g, b] = [round(c.r), round(c.g), round(c.b)];
  const byte = (n: number): string => n.toString(16).padStart(2, '0');
  const a = round(c.a, 2);
  const opaque = c.a >= 1;
  const [h, s, l] = rgbToHsl(c.r, c.g, c.b);
  const hsl = `${round(h)}, ${round(s * 100)}%, ${round(l * 100)}%`;
  return {
    hex: `#${byte(r)}${byte(g)}${byte(b)}${opaque ? '' : byte(Math.round(c.a * 255))}`,
    rgb: opaque ? `rgb(${r}, ${g}, ${b})` : `rgba(${r}, ${g}, ${b}, ${a})`,
    hsl: opaque ? `hsl(${hsl})` : `hsla(${hsl}, ${a})`,
  };
}
