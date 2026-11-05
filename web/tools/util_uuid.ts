/** UUIDs (027): generated as v4 or v7, and pasted ones explained. */

function hexOf(bytes: Uint8Array): string {
  return Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join('');
}

function withHyphens(hex: string): string {
  return `${hex.slice(0, 8)}-${hex.slice(8, 12)}-${hex.slice(12, 16)}-${hex.slice(16, 20)}-${hex.slice(20)}`;
}

/** A random UUID v4. */
export function uuidV4(): string {
  return crypto.randomUUID();
}

/** A UUID v7: the time in milliseconds first, then random bits, so they sort by time. */
export function uuidV7(now: number = Date.now()): string {
  const bytes = new Uint8Array(16);
  crypto.getRandomValues(bytes);
  let time = Math.floor(now);
  for (let i = 5; i >= 0; i--) {
    bytes[i] = time % 256;
    time = Math.floor(time / 256);
  }
  bytes[6] = 0x70 | ((bytes[6] ?? 0) & 0x0f);
  bytes[8] = 0x80 | ((bytes[8] ?? 0) & 0x3f);
  return withHyphens(hexOf(bytes));
}

export function formatUuid(uuid: string, upper: boolean, hyphens: boolean): string {
  const text = hyphens ? uuid : uuid.replaceAll('-', '');
  return upper ? text.toUpperCase() : text.toLowerCase();
}

export interface UuidInfo {
  readonly version: number;
  /** "RFC 9562", "Microsoft (reserved)", ... */
  readonly variant: string;
  /** For v1, v6 and v7: when it was made (milliseconds since 1970). */
  readonly time: number | null;
}

/** What a pasted UUID is; null if it is not one. Braces and "urn:uuid:" are accepted. */
export function explainUuid(text: string): UuidInfo | null {
  const hex = text.trim().replace(/^urn:uuid:/i, '').replace(/^\{(.*)\}$/, '$1').replaceAll('-', '').toLowerCase();
  if (!/^[0-9a-f]{32}$/.test(hex)) return null;
  const version = parseInt(hex[12] ?? '0', 16);
  const variantBits = parseInt(hex[16] ?? '0', 16);
  const variant = variantBits < 8 ? 'NCS (reserved)' : variantBits < 12 ? 'RFC 9562' : variantBits < 14 ? 'Microsoft (reserved)' : 'future (reserved)';
  let time: number | null = null;
  if (version === 7) {
    time = parseInt(hex.slice(0, 12), 16);
  } else if (version === 1 || version === 6) {
    // 100-ns intervals since 1582-10-15.
    const ticks = version === 1
      ? BigInt(`0x${hex.slice(13, 16)}${hex.slice(8, 12)}${hex.slice(0, 8)}`)
      : BigInt(`0x${hex.slice(0, 12)}${hex.slice(13, 16)}`);
    time = Number((ticks - 122192928000000000n) / 10000n);
  }
  return { version, variant, time };
}
