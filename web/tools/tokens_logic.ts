/**
 * Tokens tool (006): finding and decoding JWTs, kept free of the DOM.
 * Signatures are not verified; tokens never leave the browser.
 */
import { isRecord } from '../shared/bridge.js';

export interface DecodedJwt {
  readonly header: Record<string, unknown>;
  readonly payload: Record<string, unknown>;
}

/** JWT-shaped strings in a text (as the browser finds them in headers). */
export function findJwts(text: string): string[] {
  const found = text.match(/(?<![A-Za-z0-9_-])eyJ[A-Za-z0-9_-]*\.[A-Za-z0-9_-]+\.[A-Za-z0-9_-]*/g) ?? [];
  return found.filter((token) => token.length >= 20);
}

function decodePart(part: string): Record<string, unknown> | null {
  try {
    const base64 = part.replace(/-/g, '+').replace(/_/g, '/');
    const padded = base64 + '='.repeat((4 - (base64.length % 4)) % 4);
    const bytes = Uint8Array.from(atob(padded), (c) => c.charCodeAt(0));
    const value: unknown = JSON.parse(new TextDecoder().decode(bytes));
    return isRecord(value) ? value : null;
  } catch {
    return null;
  }
}

/** Header and payload of a JWT, or null if it is not one. */
export function decodeJwt(token: string): DecodedJwt | null {
  const parts = token.trim().split('.');
  if (parts.length !== 3 || parts[0] === undefined || parts[1] === undefined) return null;
  const header = decodePart(parts[0]);
  const payload = decodePart(parts[1]);
  return header && payload ? { header, payload } : null;
}

/** "in 12 min", "3 h ago", "in 2 days" for a time relative to `now` (both seconds). */
export function relativeTime(time: number, now: number): string {
  const delta = time - now;
  const abs = Math.abs(delta);
  let text: string;
  if (abs < 60) text = `${Math.round(abs)} s`;
  else if (abs < 3600) text = `${Math.round(abs / 60)} min`;
  else if (abs < 86400 * 2) text = `${Math.round(abs / 3600)} h`;
  else text = `${Math.round(abs / 86400)} days`;
  return delta >= 0 ? `in ${text}` : `${text} ago`;
}

export interface Claim {
  readonly label: string;
  readonly value: string;
}

const kTimeClaims: [string, string][] = [['iat', 'Issued'], ['nbf', 'Valid from'], ['exp', 'Expires']];

/** The common claims as label and text, times as local and relative time. */
export function summarizeClaims(payload: Record<string, unknown>, now: number, locale?: string): Claim[] {
  const claims: Claim[] = [];
  const text = (value: unknown): string =>
    Array.isArray(value) ? value.map(String).join(', ') : typeof value === 'string' || typeof value === 'number' ? String(value) : '';
  const add = (label: string, ...keys: string[]): void => {
    for (const key of keys) {
      const value = text(payload[key]);
      if (value !== '') {
        claims.push({ label, value });
        return;
      }
    }
  };
  add('Issuer', 'iss');
  add('Audience', 'aud');
  add('Tenant', 'tid', 'tenant', 'tenant_id');
  add('Subject', 'sub');
  add('Name', 'name', 'preferred_username', 'upn', 'email');
  add('Scopes', 'scp', 'scope', 'scopes');
  add('Roles', 'roles');
  for (const [key, label] of kTimeClaims) {
    const value = payload[key];
    if (typeof value === 'number') {
      claims.push({ label, value: `${new Date(value * 1000).toLocaleString(locale)} (${relativeTime(value, now)})` });
    }
  }
  return claims;
}

/** Whether the token's "exp" lies before `now`. */
export function isExpired(payload: Record<string, unknown>, now: number): boolean {
  return typeof payload['exp'] === 'number' && payload['exp'] < now;
}
