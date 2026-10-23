/**
 * The history page's messages to the browser (013). Every answer is checked
 * before the page uses it.
 */
import { BridgeError, isRecord, send, type Transport } from '../shared/bridge.js';
import type { Site, Visit } from './logic.js';

export interface Profile {
  readonly id: string;
  readonly name: string;
  readonly color: string;
}

export type View = 'site' | 'day';

export interface HistoryState {
  readonly profiles: readonly Profile[];
  readonly view: View;
  /** The history tab's profile. */
  readonly currentProfile: string;
}

/** One site in Clean up's list. */
export interface CleanupRow {
  readonly profile: string;
  readonly host: string;
  readonly visits: number;
  readonly lastVisit: number;
}

export interface CleanupPreview {
  readonly rows: readonly CleanupRow[];
  /** Each profile's keep list. */
  readonly keepLists: ReadonlyMap<string, readonly string[]>;
}

/** What Clear browsing data would clear in one profile. */
export interface ClearPreview {
  readonly profile: string;
  readonly visits: number;
  readonly sites: number;
  readonly cookies: number;
  readonly cacheBytes: number;
}

export interface ClearChoice {
  readonly history: boolean;
  readonly cookies: boolean;
  readonly cache: boolean;
}

/** What Clean up does in one profile. */
export interface CleanupPlan {
  readonly profile: string;
  readonly remove: readonly string[];
  /** The new keep list ("keep" mode only). */
  readonly keep?: readonly string[];
}

export interface Page {
  readonly url: string;
  readonly title: string;
  readonly visits: number;
  readonly lastVisit: number;
}

/** A part of the search the browser did not understand (string indices). */
export interface QueryProblem {
  readonly start: number;
  readonly end: number;
  readonly message: string;
}

export type SqlCell = string | number | null;

export interface SqlResult {
  readonly columns: readonly string[];
  readonly rows: readonly (readonly SqlCell[])[];
  /** Stopped at the row limit. */
  readonly truncated: boolean;
}

/** Something to delete: a page or a site of a profile, or one visit. */
export type DeleteItem =
  | { readonly profile: string; readonly kind: 'page' | 'site'; readonly value: string }
  | { readonly profile: string; readonly kind: 'visit'; readonly id: number };

function invalid(): BridgeError {
  return new BridgeError('The browser sent an answer that could not be read.');
}

function list(value: unknown, key: string): unknown[] {
  if (!isRecord(value) || !Array.isArray(value[key])) throw invalid();
  return value[key] as unknown[];
}

function text(record: Record<string, unknown>, key: string): string {
  const value = record[key];
  if (typeof value !== 'string') throw invalid();
  return value;
}

function count(record: Record<string, unknown>, key: string): number {
  const value = record[key];
  if (typeof value !== 'number' || !Number.isFinite(value) || value < 0) throw invalid();
  return value;
}

function record(value: unknown): Record<string, unknown> {
  if (!isRecord(value)) throw invalid();
  return value;
}

export function parseState(value: unknown): HistoryState {
  const answer = record(value);
  const view = answer['view'];
  return {
    profiles: list(answer, 'profiles').map((p) => {
      const item = record(p);
      return { id: text(item, 'id'), name: text(item, 'name'), color: text(item, 'color') };
    }),
    view: view === 'day' ? 'day' : 'site',
    currentProfile: typeof answer['currentProfile'] === 'string' ? answer['currentProfile'] : '',
  };
}

export function parseCleanupPreview(value: unknown): CleanupPreview {
  const answer = record(value);
  const rows = list(answer, 'rows').map((r) => {
    const item = record(r);
    return { profile: text(item, 'profile'), host: text(item, 'host'), visits: count(item, 'visits'), lastVisit: count(item, 'lastVisit') };
  });
  const lists = record(answer['keepLists']);
  const keepLists = new Map<string, readonly string[]>();
  for (const [profile, hosts] of Object.entries(lists)) {
    if (!Array.isArray(hosts)) throw invalid();
    const items: unknown[] = hosts;
    keepLists.set(profile, items.map((h) => {
      if (typeof h !== 'string') throw invalid();
      return h;
    }));
  }
  return { rows, keepLists };
}

export function parseProblems(value: unknown): QueryProblem[] {
  if (!isRecord(value) || value['problems'] === undefined) return [];
  return list(value, 'problems').map((p) => {
    const item = record(p);
    return { start: count(item, 'start'), end: count(item, 'end'), message: text(item, 'message') };
  });
}

export function parseSql(value: unknown): SqlResult {
  const answer = record(value);
  const columns = list(answer, 'columns').map((c) => {
    if (typeof c !== 'string') throw invalid();
    return c;
  });
  const rows = list(answer, 'rows').map((r) => {
    if (!Array.isArray(r) || r.length !== columns.length) throw invalid();
    const cells: unknown[] = r;
    return cells.map((cell): SqlCell => {
      if (cell === null || typeof cell === 'string' || typeof cell === 'number') return cell;
      throw invalid();
    });
  });
  const truncated = answer['truncated'];
  if (typeof truncated !== 'boolean') throw invalid();
  return { columns, rows, truncated };
}

export function parseSites(value: unknown): Site[] {
  return list(value, 'sites').map((s) => {
    const item = record(s);
    return {
      profile: text(item, 'profile'),
      host: text(item, 'host'),
      visits: count(item, 'visits'),
      pages: count(item, 'pages'),
      lastVisit: count(item, 'lastVisit'),
    };
  });
}

export function parsePages(value: unknown): Page[] {
  return list(value, 'pages').map((p) => {
    const item = record(p);
    return {
      url: text(item, 'url'),
      title: text(item, 'title'),
      visits: count(item, 'visits'),
      lastVisit: count(item, 'lastVisit'),
    };
  });
}

export function parseVisits(value: unknown): Visit[] {
  return list(value, 'visits').map((v) => {
    const item = record(v);
    return {
      profile: text(item, 'profile'),
      id: count(item, 'id'),
      url: text(item, 'url'),
      title: text(item, 'title'),
      host: text(item, 'host'),
      time: count(item, 'time'),
      transition: text(item, 'transition'),
    };
  });
}

function profileArgs(profile: string | null): Record<string, unknown> {
  return profile === null ? {} : { profile };
}

function queryArgs(query: string): Record<string, unknown> {
  return query.trim() === '' ? {} : { query };
}

export class HistoryApi {
  readonly #transport: Transport | null;

  constructor(transport: Transport | null) {
    this.#transport = transport;
  }

  async state(): Promise<HistoryState> {
    return parseState(await send('history.state', {}, this.#transport));
  }

  async setView(view: View): Promise<void> {
    await send('history.setView', { view }, this.#transport);
  }

  /** Sites of one profile, or of all profiles (null), matching `query`. */
  async sites(profile: string | null, query = ''): Promise<{ sites: Site[]; problems: QueryProblem[] }> {
    const answer = await send('history.sites', { ...profileArgs(profile), ...queryArgs(query) }, this.#transport);
    return { sites: parseSites(answer), problems: parseProblems(answer) };
  }

  async pages(profile: string, host: string, query = ''): Promise<Page[]> {
    return parsePages(await send('history.pages', { profile, host, ...queryArgs(query) }, this.#transport));
  }

  /** Visits before `before` (seconds; 0 for now), newest first. */
  async visits(
    profile: string | null,
    before: number,
    limit: number,
    query = '',
  ): Promise<{ visits: Visit[]; problems: QueryProblem[] }> {
    const answer = await send(
      'history.visits',
      { ...profileArgs(profile), before, limit, ...queryArgs(query) },
      this.#transport,
    );
    return { visits: parseVisits(answer), problems: parseProblems(answer) };
  }

  async remove(items: readonly DeleteItem[]): Promise<void> {
    await send('history.delete', { items }, this.#transport);
  }

  /** `profile` null: the history tab's own profile. */
  async open(profile: string | null, url: string, newTab: boolean): Promise<void> {
    await send('history.open', { ...profileArgs(profile), url, newTab }, this.#transport);
  }

  /** Runs a read-only SELECT over the views pages and visits. */
  async sql(query: string): Promise<SqlResult> {
    return parseSql(await send('history.sql', { query }, this.#transport));
  }

  /** Sites of one profile or all (null) with visits since `since` (seconds; 0 all time). */
  async cleanupPreview(profile: string | null, since: number): Promise<CleanupPreview> {
    return parseCleanupPreview(await send('history.cleanup.preview', { ...profileArgs(profile), since }, this.#transport));
  }

  /** Deletes the planned visits since `since`; resolves with how many went. */
  async cleanupRun(since: number, profiles: readonly CleanupPlan[]): Promise<number> {
    const answer = await send('history.cleanup.run', { since, profiles }, this.#transport);
    return count(record(answer), 'visits');
  }

  async clearPreview(profile: string | null, since: number): Promise<ClearPreview[]> {
    const answer = await send('history.clear.preview', { ...profileArgs(profile), since }, this.#transport);
    return list(answer, 'profiles').map((p) => {
      const item = record(p);
      return {
        profile: text(item, 'profile'),
        visits: count(item, 'visits'),
        sites: count(item, 'sites'),
        cookies: typeof item['cookies'] === 'number' && item['cookies'] >= -1 ? item['cookies'] : count(item, 'cookies'),
        cacheBytes: count(item, 'cacheBytes'),
      };
    });
  }

  /** Resolves with false if the cookies could not be cleared. */
  async clear(profile: string | null, since: number, choice: ClearChoice): Promise<boolean> {
    const answer = await send('history.clear.run', { ...profileArgs(profile), since, ...choice }, this.#transport);
    return !(isRecord(answer) && answer['cookiesFailed'] === true);
  }

  async copy(text: string): Promise<void> {
    await send('history.copy', { text }, this.#transport);
  }
}
