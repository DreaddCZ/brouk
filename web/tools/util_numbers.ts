/** Number bases and text to hex bytes (027). */
import { bytesToHex, failed, ok, utf8Text, type Converted } from './util_common.js';

export type Base = 2 | 8 | 10 | 16;

const kDigits: Record<Base, RegExp> = { 2: /^[01]+$/, 8: /^[0-7]+$/, 10: /^\d+$/, 16: /^[0-9a-f]+$/i };
const kPrefix: Record<Base, string> = { 2: '0b', 8: '0o', 10: '', 16: '0x' };

/** A whole number written in `base` (a matching 0x/0o/0b prefix, "_" and spaces allowed), of any size. */
export function parseInBase(text: string, base: Base): bigint | null {
  let clean = text.trim().replace(/[_\s]/g, '');
  const negative = clean.startsWith('-');
  if (negative) clean = clean.slice(1);
  const prefix = kPrefix[base];
  if (prefix !== '' && clean.toLowerCase().startsWith(prefix)) clean = clean.slice(2);
  if (!kDigits[base].test(clean)) return null;
  const value = base === 10 ? BigInt(clean) : BigInt(`${prefix}${clean}`);
  return negative ? -value : value;
}

export function formatInBase(value: bigint, base: Base): string {
  return value.toString(base);
}

/** UTF-8 bytes of the text as hex. */
export function textToHex(text: string): string {
  return bytesToHex(new TextEncoder().encode(text));
}

/** Hex bytes (spaces, colons and 0x allowed) back to UTF-8 text. */
export function hexToText(hex: string): Converted {
  const clean = hex.replace(/0x/gi, '').replace(/[\s:,-]/g, '');
  if (!/^[0-9a-f]*$/i.test(clean)) return failed('Not hex: only 0–9 and a–f are allowed.');
  if (clean.length % 2 !== 0) return failed('Not whole bytes: an odd number of hex digits.');
  const bytes = new Uint8Array(clean.length / 2);
  for (let i = 0; i < bytes.length; i++) bytes[i] = parseInt(clean.slice(i * 2, i * 2 + 2), 16);
  const text = utf8Text(bytes);
  return text === null ? failed('The bytes are not UTF-8 text.') : ok(text);
}
