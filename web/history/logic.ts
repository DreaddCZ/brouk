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
