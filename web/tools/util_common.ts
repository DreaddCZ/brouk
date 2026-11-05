/** Shared by the utilities (027): results and byte helpers. */

/** A conversion's result: the text, or why the input cannot be converted. */
export type Converted =
  | { readonly ok: true; readonly text: string; readonly note?: string }
  | { readonly ok: false; readonly error: string };

/** `note` is shown with the result, e.g. "Not text: 12 bytes as hex". */
export function ok(text: string, note?: string): Converted {
  return note === undefined ? { ok: true, text } : { ok: true, text, note };
}

export function failed(error: string): Converted {
  return { ok: false, error };
}

export function bytesToHex(bytes: Uint8Array, separator = ' '): string {
  return Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join(separator);
}

export function bytesToBase64(bytes: Uint8Array): string {
  let binary = '';
  // In chunks: String.fromCharCode takes a limited number of arguments.
  for (let i = 0; i < bytes.length; i += 0x8000) {
    binary += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  }
  return btoa(binary);
}

/** UTF-8 text of the bytes, or null when they are not valid UTF-8. */
export function utf8Text(bytes: Uint8Array): string | null {
  try {
    return new TextDecoder('utf-8', { fatal: true }).decode(bytes);
  } catch {
    return null;
  }
}

/** "1 byte", "3 bytes". */
export function byteCount(n: number): string {
  return n === 1 ? '1 byte' : `${n} bytes`;
}
