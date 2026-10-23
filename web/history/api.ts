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
}

export interface Page {
  readonly url: string;
  readonly title: string;
  readonly visits: number;
  readonly lastVisit: number;
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
  };
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

  /** Sites of one profile, or of all profiles (null). */
  async sites(profile: string | null): Promise<Site[]> {
    return parseSites(await send('history.sites', profileArgs(profile), this.#transport));
  }

  async pages(profile: string, host: string): Promise<Page[]> {
    return parsePages(await send('history.pages', { profile, host }, this.#transport));
  }

  /** Visits before `before` (seconds; 0 for now), newest first. */
  async visits(profile: string | null, before: number, limit: number): Promise<Visit[]> {
    return parseVisits(
      await send('history.visits', { ...profileArgs(profile), before, limit }, this.#transport),
    );
  }

  async remove(items: readonly DeleteItem[]): Promise<void> {
    await send('history.delete', { items }, this.#transport);
  }

  async open(profile: string, url: string, newTab: boolean): Promise<void> {
    await send('history.open', { profile, url, newTab }, this.#transport);
  }
}
