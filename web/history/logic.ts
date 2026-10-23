/** History page rules kept free of the DOM (013). */

export interface Site {
  readonly profile: string;
  readonly host: string;
  readonly visits: number;
  readonly pages: number;
  /** Seconds since 1970. */
  readonly lastVisit: number;
}

export interface Visit {
  readonly profile: string;
  readonly id: number;
  readonly url: string;
  readonly title: string;
  readonly host: string;
  readonly time: number;
  readonly transition: string;
}

export type SiteSort = 'lastVisit' | 'visits' | 'host';

/** Sites in the order the user chose; ties keep the newest first. */
export function sortSites(sites: readonly Site[], sort: SiteSort): Site[] {
  const sorted = [...sites];
  sorted.sort((a, b) => {
    if (sort === 'visits' && a.visits !== b.visits) return b.visits - a.visits;
    if (sort === 'host') {
      const byHost = hostLabel(a.host).localeCompare(hostLabel(b.host));
      if (byHost !== 0) return byHost;
    }
    return b.lastVisit - a.lastVisit;
  });
  return sorted;
}

/** Local files have no host. */
export function hostLabel(host: string): string {
  return host === '' ? 'Local files' : host;
}

/** The part of a URL after its host, for listing a site's pages. */
export function pathOf(url: string): string {
  try {
    const parsed = new URL(url);
    if (parsed.protocol === 'file:') return parsed.pathname;
    const rest = parsed.pathname + parsed.search + parsed.hash;
    return rest === '' ? '/' : rest;
  } catch {
    return url;
  }
}

export interface DayGroup {
  readonly label: string;
  readonly visits: readonly Visit[];
}

function startOfDay(date: Date): number {
  return new Date(date.getFullYear(), date.getMonth(), date.getDate()).getTime();
}

/** "Today", "Yesterday", or the date ("Monday, 21 October 2026"). */
export function dayLabel(seconds: number, now: Date, locale?: string): string {
  const date = new Date(seconds * 1000);
  const days = Math.round((startOfDay(now) - startOfDay(date)) / 86_400_000);
  if (days === 0) return 'Today';
  if (days === 1) return 'Yesterday';
  return date.toLocaleDateString(locale, { weekday: 'long', day: 'numeric', month: 'long', year: 'numeric' });
}

/** Visits (newest first) grouped by local day, in order. */
export function groupByDay(visits: readonly Visit[], now: Date, locale?: string): DayGroup[] {
  const groups: { label: string; visits: Visit[] }[] = [];
  for (const visit of visits) {
    const label = dayLabel(visit.time, now, locale);
    const last = groups[groups.length - 1];
    if (last !== undefined && last.label === label) {
      last.visits.push(visit);
    } else {
      groups.push({ label, visits: [visit] });
    }
  }
  return groups;
}

/** When a site or page was last visited: the time today, else the date. */
export function lastVisitLabel(seconds: number, now: Date, locale?: string): string {
  const date = new Date(seconds * 1000);
  if (startOfDay(date) === startOfDay(now)) {
    return date.toLocaleTimeString(locale, { hour: '2-digit', minute: '2-digit' });
  }
  const sameYear = date.getFullYear() === now.getFullYear();
  return date.toLocaleDateString(locale, sameYear
    ? { day: 'numeric', month: 'short' }
    : { day: 'numeric', month: 'short', year: 'numeric' });
}

/**
 * Checkbox selection: a plain click toggles one key; Shift-click sets the
 * whole range from the last clicked key to this one to the clicked key's new
 * state, as in Chrome's history page.
 */
export function updateSelection(
  selected: ReadonlySet<string>,
  order: readonly string[],
  anchor: string | null,
  key: string,
  shift: boolean,
): Set<string> {
  const next = new Set(selected);
  const on = !selected.has(key);
  const from = anchor === null ? -1 : order.indexOf(anchor);
  const to = order.indexOf(key);
  if (shift && from >= 0 && to >= 0) {
    const [start, end] = from < to ? [from, to] : [to, from];
    for (const item of order.slice(start, end + 1)) {
      if (on) next.add(item);
      else next.delete(item);
    }
  } else if (on) {
    next.add(key);
  } else {
    next.delete(key);
  }
  return next;
}

/** A filter as the search box writes it: field:value, quoted if needed. */
export function filterText(field: string, value: string): string {
  const clean = value.replaceAll('"', '').trim();
  return /\s/.test(clean) ? `${field}:"${clean}"` : `${field}:${clean}`;
}

/** The search with `filter` added at the end (a chip was used). */
export function addFilter(query: string, filter: string): string {
  const trimmed = query.trim();
  return trimmed === '' ? filter : `${trimmed} ${filter}`;
}

export interface Segment {
  readonly text: string;
  /** The hint for a part that was not understood. */
  readonly problem?: string;
}

/** The search split into parts, those not understood marked (underlined). */
export function querySegments(query: string, problems: readonly { start: number; end: number; message: string }[]): Segment[] {
  const sorted = [...problems].filter((p) => p.end > p.start).sort((a, b) => a.start - b.start);
  const segments: Segment[] = [];
  let at = 0;
  for (const problem of sorted) {
    if (problem.start < at) continue;
    if (problem.start > at) segments.push({ text: query.slice(at, problem.start) });
    segments.push({ text: query.slice(problem.start, problem.end), problem: problem.message });
    at = problem.end;
  }
  if (at < query.length) segments.push({ text: query.slice(at) });
  return segments;
}

/** Results as CSV (RFC 4180): fields with commas, quotes or line breaks quoted. */
export function toCsv(columns: readonly string[], rows: readonly (readonly (string | number | null)[])[]): string {
  const field = (value: string | number | null): string => {
    const text = value === null ? '' : String(value);
    return /[",\r\n]/.test(text) ? `"${text.replaceAll('"', '""')}"` : text;
  };
  return [columns, ...rows].map((row) => row.map(field).join(',')).join('\r\n') + '\r\n';
}

/** Whether a result cell is an address that can be opened (web or file). */
export function isOpenableUrl(value: string | number | null): value is string {
  return typeof value === 'string' && /^(https?:\/\/|file:\/\/)\S+$/i.test(value);
}

/** Example queries next to the SQL editor. */
export const kSqlExamples: readonly { readonly label: string; readonly sql: string }[] = [
  { label: 'Most visited pages', sql: 'SELECT profile, title, url, visit_count\nFROM pages\nORDER BY visit_count DESC\nLIMIT 50' },
  { label: 'Sites by visits this week', sql: "SELECT profile, host, count(*) AS visits\nFROM visits\nWHERE visit_time >= datetime('now', 'localtime', '-7 days')\nGROUP BY profile, host\nORDER BY visits DESC" },
  { label: 'Visits per day', sql: 'SELECT date(visit_time) AS day, count(*) AS visits\nFROM visits\nGROUP BY day\nORDER BY day DESC' },
  { label: 'Typed addresses', sql: "SELECT profile, url, visit_time\nFROM visits\nWHERE transition = 'typed'\nORDER BY visit_time DESC" },
];

// Clean up (013).

export interface CleanupSite {
  readonly profile: string;
  readonly host: string;
  readonly visits: number;
}

export type CleanupMode = 'keep' | 'delete';

export function cleanupKey(profile: string, host: string): string {
  return `${profile}\u0000${host}`;
}

/**
 * What "keep" mode starts with, per profile: its keep list and its 10 most
 * visited sites (the suggestions). Rows come most visited first.
 */
export function preselectCleanup(
  rows: readonly CleanupSite[],
  keepLists: ReadonlyMap<string, readonly string[]>,
  suggestions = 10,
): { selected: Set<string>; suggested: Set<string> } {
  const selected = new Set<string>();
  const suggested = new Set<string>();
  const counts = new Map<string, number>();
  for (const row of rows) {
    const key = cleanupKey(row.profile, row.host);
    if (keepLists.get(row.profile)?.includes(row.host) === true) selected.add(key);
    const seen = counts.get(row.profile) ?? 0;
    if (seen < suggestions) {
      counts.set(row.profile, seen + 1);
      suggested.add(key);
      selected.add(key);
    }
  }
  return { selected, suggested };
}

export interface CleanupOutcome {
  readonly plans: { profile: string; remove: string[]; keep?: string[] }[];
  readonly visits: number;
  readonly sites: number;
}

/**
 * What Clean up will do: "keep" removes every listed site except the checked
 * ones and makes the checked ones the keep list (entries of the old list
 * that are not in this time range stay on it); "delete" removes the checked
 * sites and leaves the keep list alone.
 */
export function planCleanup(
  rows: readonly CleanupSite[],
  selected: ReadonlySet<string>,
  mode: CleanupMode,
  keepLists: ReadonlyMap<string, readonly string[]>,
): CleanupOutcome {
  const byProfile = new Map<string, { remove: string[]; keep: string[]; listed: Set<string> }>();
  let visits = 0;
  let sites = 0;
  for (const row of rows) {
    let plan = byProfile.get(row.profile);
    if (plan === undefined) {
      plan = { remove: [], keep: [], listed: new Set() };
      byProfile.set(row.profile, plan);
    }
    plan.listed.add(row.host);
    const checked = selected.has(cleanupKey(row.profile, row.host));
    const remove = mode === 'keep' ? !checked : checked;
    if (remove) {
      plan.remove.push(row.host);
      visits += row.visits;
      sites += 1;
    } else if (mode === 'keep') {
      plan.keep.push(row.host);
    }
  }
  const plans = [...byProfile.entries()].map(([profile, plan]) => {
    if (mode === 'delete') return { profile, remove: plan.remove };
    const older = (keepLists.get(profile) ?? []).filter((host) => !plan.listed.has(host));
    return { profile, remove: plan.remove, keep: [...older, ...plan.keep] };
  });
  return { plans, visits, sites };
}

/** Clean up's time ranges, as the start time in seconds (0: all time). */
export function cleanupSince(range: string, nowSeconds: number): number {
  switch (range) {
    case '24h': return nowSeconds - 86_400;
    case '7d': return nowSeconds - 7 * 86_400;
    case '4w': return nowSeconds - 28 * 86_400;
    default: return 0;
  }
}
