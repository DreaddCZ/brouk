/** Site data tool (006): parsing and formatting, kept free of the DOM. */
import { isRecord } from '../shared/bridge.js';

export interface Cookie {
  readonly name: string;
  readonly value: string;
  readonly domain: string;
  readonly path: string;
  /** Seconds since 1970; -1 for session cookies. */
  readonly expires: number;
  readonly httpOnly: boolean;
  readonly secure: boolean;
  readonly sameSite: string;
}

/** The origin of a page URL, or null for pages without a site (about:, data:). */
export function siteOrigin(url: string): string | null {
  try {
    const parsed = new URL(url);
    if (parsed.protocol !== 'http:' && parsed.protocol !== 'https:') return null;
    return parsed.origin;
  } catch {
    return null;
  }
}

/** Cookies from a Network.getCookies result; malformed entries are skipped. */
export function parseCookies(result: unknown): Cookie[] {
  if (!isRecord(result) || !Array.isArray(result['cookies'])) return [];
  const list: unknown[] = result['cookies'];
  const cookies: Cookie[] = [];
  for (const item of list) {
    if (!isRecord(item) || typeof item['name'] !== 'string' || typeof item['value'] !== 'string' ||
        typeof item['domain'] !== 'string' || typeof item['path'] !== 'string') {
      continue;
    }
    cookies.push({
      name: item['name'],
      value: item['value'],
      domain: item['domain'],
      path: item['path'],
      expires: typeof item['expires'] === 'number' && item['session'] !== true ? item['expires'] : -1,
      httpOnly: item['httpOnly'] === true,
      secure: item['secure'] === true,
      sameSite: typeof item['sameSite'] === 'string' ? item['sameSite'] : '',
    });
  }
  return cookies.sort((a, b) => a.name.localeCompare(b.name) || a.domain.localeCompare(b.domain));
}

/** Storage entries from DOMStorage.getDOMStorageItems, sorted by key. */
export function parseStorageItems(result: unknown): [string, string][] {
  if (!isRecord(result) || !Array.isArray(result['entries'])) return [];
  const list: unknown[] = result['entries'];
  const entries: [string, string][] = [];
  for (const entry of list) {
    if (Array.isArray(entry) && typeof entry[0] === 'string' && typeof entry[1] === 'string') {
      entries.push([entry[0], entry[1]]);
    }
  }
  return entries.sort((a, b) => a[0].localeCompare(b[0]));
}

/** "Session", or the local date and time a cookie expires. */
export function formatExpiry(expires: number, locale?: string): string {
  if (expires < 0) return 'Session';
  return new Date(expires * 1000).toLocaleString(locale);
}

/** 1536 -> "1.5 kB". */
export function formatBytes(bytes: number): string {
  if (bytes < 1000) return `${bytes} B`;
  const units = ['kB', 'MB', 'GB'];
  let value = bytes;
  let unit = '';
  for (const next of units) {
    value /= 1000;
    unit = next;
    if (value < 1000) break;
  }
  return `${value.toFixed(value < 10 ? 1 : 0)} ${unit}`;
}

/**
 * The Network.setCookie parameters for a cookie edited or added on `origin`.
 * New cookies default to the page's host and path "/".
 */
export function setCookieParams(cookie: Cookie, origin: string): Record<string, unknown> {
  const params: Record<string, unknown> = {
    name: cookie.name,
    value: cookie.value,
    path: cookie.path === '' ? '/' : cookie.path,
    secure: cookie.secure,
    httpOnly: cookie.httpOnly,
  };
  if (cookie.domain === '') {
    params['url'] = origin;
  } else {
    params['domain'] = cookie.domain;
  }
  if (cookie.sameSite !== '') params['sameSite'] = cookie.sameSite;
  if (cookie.expires >= 0) params['expires'] = cookie.expires;
  return params;
}
