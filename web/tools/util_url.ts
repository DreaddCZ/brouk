/** URL encoding and the URL parser (027). */
import { failed, ok, type Converted } from './util_common.js';

/** component: like encodeURIComponent; url: keeps the URL's own characters; form: form data (space as "+"). */
export type UrlMode = 'component' | 'url' | 'form';

export function encodeUrl(text: string, mode: UrlMode): Converted {
  try {
    if (mode === 'component') return ok(encodeURIComponent(text));
    if (mode === 'url') return ok(encodeURI(text));
    return ok(new URLSearchParams({ x: text }).toString().slice(2));
  } catch {
    // A lone surrogate cannot be encoded.
    return failed('The text contains a character that cannot be encoded (a broken surrogate pair).');
  }
}

/** Decodes percent-encoding; invalid sequences and bytes that are not UTF-8 are reported with their position. */
export function decodeUrl(text: string, mode: UrlMode): Converted {
  const input = mode === 'form' ? text.replaceAll('+', ' ') : text;
  let result = '';
  let i = 0;
  while (i < input.length) {
    if (input[i] !== '%') {
      result += input[i];
      i++;
      continue;
    }
    // A run of %XX sequences is one piece of UTF-8.
    const start = i;
    const bytes: number[] = [];
    while (input[i] === '%') {
      const hex = input.slice(i + 1, i + 3);
      if (!/^[0-9A-Fa-f]{2}$/.test(hex)) {
        return failed(`Not valid: “${input.slice(i, i + 3)}” at position ${i + 1} is not a % sequence (% and two hex digits).`);
      }
      bytes.push(parseInt(hex, 16));
      i += 3;
    }
    try {
      result += new TextDecoder('utf-8', { fatal: true }).decode(new Uint8Array(bytes));
    } catch {
      return failed(`Not valid: “${input.slice(start, i)}” at position ${start + 1} is not UTF-8 text.`);
    }
  }
  return ok(result);
}

export interface UrlParts {
  readonly protocol: string;
  readonly username: string;
  readonly password: string;
  readonly hostname: string;
  readonly port: string;
  readonly pathname: string;
  /** Decoded names and values, in order (a name may repeat). */
  readonly params: readonly (readonly [string, string])[];
  readonly hash: string;
}

export function parseUrl(text: string): { parts: UrlParts } | { error: string } {
  let url: URL;
  try {
    url = new URL(text.trim());
  } catch {
    return { error: 'Not a full URL (it needs a scheme, e.g. https://).' };
  }
  return {
    parts: {
      protocol: url.protocol.replace(/:$/, ''),
      username: decodeURIComponent(url.username),
      password: decodeURIComponent(url.password),
      hostname: url.hostname,
      port: url.port,
      pathname: url.pathname,
      params: [...url.searchParams.entries()],
      hash: url.hash.replace(/^#/, ''),
    },
  };
}

/** The URL of edited parts; null if they do not make one. */
export function buildUrl(parts: UrlParts): string | null {
  let url: URL;
  try {
    url = new URL(`${parts.protocol}://${parts.hostname || 'invalid'}`);
    url.hostname = parts.hostname;
    url.port = parts.port;
    url.username = parts.username;
    url.password = parts.password;
    url.pathname = parts.pathname;
    const search = new URLSearchParams();
    for (const [name, value] of parts.params) search.append(name, value);
    url.search = search.toString();
    url.hash = parts.hash;
  } catch {
    return null;
  }
  return url.toString();
}
