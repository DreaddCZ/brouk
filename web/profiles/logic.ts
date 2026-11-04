/** Rules and labels of the profile manager page, kept free of the DOM. */
import type { Profile } from './api.js';

/** Bytes, as the browser counts them (UTF-8). */
const kMaxNameBytes = 64;

/**
 * Why a name cannot be used, or null. The browser checks again; this only
 * gives quick feedback while typing.
 */
export function nameProblem(
  name: string,
  profiles: readonly Profile[],
  exceptId: string | null,
): string | null {
  const trimmed = name.trim();
  if (trimmed === '') {
    return 'Enter a name.';
  }
  if (new TextEncoder().encode(trimmed).length > kMaxNameBytes) {
    return 'The name is too long.';
  }
  if (/[\u0000-\u001f\u007f]/.test(trimmed)) {
    return 'The name cannot contain line breaks or control characters.';
  }
  const lower = trimmed.toLowerCase();
  const other = profiles.find((p) => p.id !== exceptId && p.name.toLowerCase() === lower);
  if (other !== undefined) {
    return `Another profile is already called “${other.name}”.`;
  }
  return null;
}

export function colorLabel(color: string): string {
  return color.charAt(0).toUpperCase() + color.slice(1);
}

export function tabsLabel(count: number): string {
  if (count === 0) {
    return 'No open tabs';
  }
  return count === 1 ? '1 open tab' : `${count} open tabs`;
}

/** How much history goes into a profile export (022). */
export interface HistoryChoice {
  readonly kind: 'days' | 'pages';
  readonly count: number;
}

/** The browser's limits for the export's history. */
export const kHistoryMaxDays = 3650;
export const kHistoryMaxPages = 20000;

/**
 * The export's history choice from the form: null when history is not
 * included, else the limit, or a problem to show.
 */
export function historyChoice(
  include: boolean,
  kind: 'days' | 'pages',
  countText: string,
): { choice: HistoryChoice | null } | { problem: string } {
  if (!include) {
    return { choice: null };
  }
  const max = kind === 'days' ? kHistoryMaxDays : kHistoryMaxPages;
  const trimmed = countText.trim();
  const count = Number(trimmed);
  if (!/^[0-9]+$/.test(trimmed) || !Number.isSafeInteger(count) || count < 1 || count > max) {
    return { problem: `Enter a number of ${kind} from 1 to ${max}.` };
  }
  return { choice: { kind, count } };
}
