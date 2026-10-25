/** What the passwords page shows, as pure functions (tested without a DOM). */
import type { Checkup, CheckupEntry, Entry } from './api.js';

/** The host to show for an origin: no scheme, no "www.", the port kept. */
export function displayHost(origin: string): string {
  const host = origin.replace(/^[a-z]+:\/\//, '');
  return host.startsWith('www.') ? host.slice(4) : host;
}

/** Entries of one profile ('' for all) whose site or user name contains
 * every word of the query (case ignored), sorted by site and user name. */
export function filterEntries(entries: readonly Entry[], profile: string, query: string): Entry[] {
  const words = query.toLowerCase().split(/\s+/).filter((w) => w !== '');
  return entries
    .filter((e) => profile === '' || e.profile === profile)
    .filter((e) => {
      const hay = [e.origin, ...e.others, e.username].join(' ').toLowerCase();
      return words.every((w) => hay.includes(w));
    })
    .sort((a, b) =>
      displayHost(a.origin).localeCompare(displayHost(b.origin)) || a.username.localeCompare(b.username) || a.id - b.id);
}

/** "today", "yesterday", "3 days ago", "2 months ago", "never". */
export function ago(seconds: number, now: number): string {
  if (seconds <= 0) return 'never';
  const days = Math.floor((now - seconds) / 86400);
  if (days <= 0) return 'today';
  if (days === 1) return 'yesterday';
  if (days < 60) return `${days} days ago`;
  const months = Math.floor(days / 30);
  if (months < 24) return `${months} months ago`;
  return `${Math.floor(days / 365)} years ago`;
}

/** The other sites field: comma or line separated. */
export function parseOthers(text: string): string[] {
  return text.split(/[,\n]/).map((s) => s.trim()).filter((s) => s !== '');
}

export interface CheckupResult {
  readonly weak: readonly CheckupEntry[];
  readonly reused: readonly (readonly CheckupEntry[])[];
  readonly breached: readonly CheckupEntry[];
}

/** Scores of 0–2 (zxcvbn: guessable within hours to months) count as weak. */
export const kWeakScore = 2;

/** Sorts the checkup into its three lists; `scores` are per entry. */
export function checkupResult(checkup: Checkup, scores: readonly number[]): CheckupResult {
  return {
    weak: checkup.entries.filter((_, i) => (scores[i] ?? 4) <= kWeakScore),
    reused: checkup.reused.map((group) =>
      group.map((i) => checkup.entries[i]).filter((e): e is CheckupEntry => e !== undefined)),
    breached: checkup.entries.filter((e) => e.breached > 0).sort((a, b) => b.breached - a.breached),
  };
}
