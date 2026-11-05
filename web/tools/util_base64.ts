/** Base64 (027): text and files, standard and URL-safe alphabet. */
import { byteCount, bytesToBase64, bytesToHex, failed, ok, utf8Text, type Converted } from './util_common.js';

export function encodeBase64(text: string, urlSafe: boolean): string {
  const base64 = bytesToBase64(new TextEncoder().encode(text));
  return urlSafe ? toUrlSafe(base64) : base64;
}

export function toUrlSafe(base64: string): string {
  return base64.replaceAll('+', '-').replaceAll('/', '_').replace(/=+$/, '');
}

/** The bytes of Base64 text (either alphabet; spaces and line breaks ignored), or why not. */
export function decodeBase64Bytes(input: string): { bytes: Uint8Array } | { error: string } {
  const urlSafe = /[-_]/.test(input);
  let clean = '';
  for (let i = 0; i < input.length; i++) {
    const c = input[i] ?? '';
    if (/\s/.test(c)) continue;
    const valid = /[A-Za-z0-9]/.test(c) || (urlSafe ? c === '-' || c === '_' : c === '+' || c === '/') || c === '=';
    if (!valid) return { error: `Not valid Base64: unexpected character “${c}” at position ${i + 1}.` };
    clean += c;
  }
  const padding = clean.indexOf('=');
  if (padding >= 0 && !/^=+$/.test(clean.slice(padding))) {
    return { error: `Not valid Base64: “=” in the middle at position ${input.indexOf('=') + 1}.` };
  }
  let body = padding >= 0 ? clean.slice(0, padding) : clean;
  if (body.length % 4 === 1) return { error: 'Not valid Base64: the length is wrong (one character too many or missing).' };
  if (urlSafe) body = body.replaceAll('-', '+').replaceAll('_', '/');
  body += '='.repeat((4 - (body.length % 4)) % 4);
  const binary = atob(body);
  return { bytes: Uint8Array.from(binary, (c) => c.charCodeAt(0)) };
}

/** Decoded text; bytes that are not text are shown as hex with their count. */
export function decodeBase64(input: string): Converted {
  const decoded = decodeBase64Bytes(input);
  if ('error' in decoded) return failed(decoded.error);
  const text = utf8Text(decoded.bytes);
  if (text !== null) return ok(text);
  return ok(bytesToHex(decoded.bytes), `Not text: ${byteCount(decoded.bytes.length)}, shown as hex.`);
}

/** A file's Base64 and its data URL. */
export function fileBase64(bytes: Uint8Array, type: string): { base64: string; dataUrl: string } {
  const base64 = bytesToBase64(bytes);
  return { base64, dataUrl: `data:${type || 'application/octet-stream'};base64,${base64}` };
}
