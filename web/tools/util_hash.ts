/** Hashes and HMAC (027), with the browser's Web Crypto. */
import { bytesToBase64, bytesToHex } from './util_common.js';

export const kHashAlgorithms = ['SHA-1', 'SHA-256', 'SHA-384', 'SHA-512'] as const;
export type HashAlgorithm = (typeof kHashAlgorithms)[number];

export interface Digest {
  readonly hex: string;
  readonly base64: string;
}

function digestOf(buffer: ArrayBuffer): Digest {
  const bytes = new Uint8Array(buffer);
  return { hex: bytesToHex(bytes, ''), base64: bytesToBase64(bytes) };
}

/** The hash of `data`, or with `key` (UTF-8 text) its HMAC. */
export async function hash(data: Uint8Array<ArrayBuffer>, algorithm: HashAlgorithm, key: string | null): Promise<Digest> {
  if (key === null) {
    return digestOf(await crypto.subtle.digest(algorithm, data));
  }
  const imported = await crypto.subtle.importKey(
    'raw', new TextEncoder().encode(key), { name: 'HMAC', hash: algorithm }, false, ['sign']);
  return digestOf(await crypto.subtle.sign('HMAC', imported, data));
}
