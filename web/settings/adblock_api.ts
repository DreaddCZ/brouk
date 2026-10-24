/**
 * The settings page's ad blocking messages (007). Every answer is checked
 * before the page uses it.
 */
import { BridgeError, isRecord, send, type Transport } from '../shared/bridge.js';

export interface FilterCategory {
  readonly id: string;
  readonly label: string;
}

export interface FilterList {
  readonly id: string;
  readonly name: string;
  readonly category: string;
  readonly url: string;
  readonly enabled: boolean;
  /** Added by URL (can be removed). */
  readonly custom: boolean;
  /** Last successful download, seconds since 1970; 0 if never. */
  readonly updated: number;
  /** Of the last attempt; '' if it worked. */
  readonly error: string;
}

export interface AdblockState {
  readonly categories: readonly FilterCategory[];
  readonly lists: readonly FilterList[];
  readonly updating: boolean;
  /** False until the browser has started the ad blocker. */
  readonly ready: boolean;
}

export interface FilterProblem {
  readonly line: number;
  readonly error: string;
}

function invalid(): BridgeError {
  return new BridgeError('The browser sent an answer that could not be read.');
}

function record(value: unknown): Record<string, unknown> {
  if (!isRecord(value)) throw invalid();
  return value;
}

function text(item: Record<string, unknown>, key: string): string {
  const value = item[key];
  if (typeof value !== 'string') throw invalid();
  return value;
}

function list(item: Record<string, unknown>, key: string): unknown[] {
  const value = item[key];
  if (!Array.isArray(value)) throw invalid();
  return value as unknown[];
}

export function parseAdblockState(value: unknown): AdblockState {
  const answer = record(value);
  return {
    categories: list(answer, 'categories').map((c) => {
      const item = record(c);
      return { id: text(item, 'id'), label: text(item, 'label') };
    }),
    lists: list(answer, 'lists').map((l) => {
      const item = record(l);
      const updated = item['updated'];
      return {
        id: text(item, 'id'),
        name: text(item, 'name'),
        category: text(item, 'category'),
        url: text(item, 'url'),
        enabled: item['enabled'] === true,
        custom: item['custom'] === true,
        updated: typeof updated === 'number' && Number.isFinite(updated) ? updated : 0,
        error: text(item, 'error'),
      };
    }),
    updating: answer['updating'] === true,
    ready: answer['ready'] === true,
  };
}

export function parseProblems(value: unknown): FilterProblem[] {
  return list(record(value), 'problems').map((p) => {
    const item = record(p);
    const line = item['line'];
    if (typeof line !== 'number' || !Number.isInteger(line) || line < 1) throw invalid();
    return { line, error: text(item, 'error') };
  });
}

export class AdblockApi {
  readonly #transport: Transport | null;

  constructor(transport: Transport | null) {
    this.#transport = transport;
  }

  async state(): Promise<AdblockState> {
    return parseAdblockState(await send('adblock.state', {}, this.#transport));
  }

  async setList(id: string, enabled: boolean): Promise<void> {
    await send('adblock.setList', { id, enabled }, this.#transport);
  }

  async addList(category: string, url: string): Promise<void> {
    await send('adblock.addList', { category, url }, this.#transport);
  }

  async removeList(id: string): Promise<void> {
    await send('adblock.removeList', { id }, this.#transport);
  }

  async update(): Promise<void> {
    await send('adblock.update', {}, this.#transport);
  }

  async myFilters(): Promise<{ text: string; problems: FilterProblem[] }> {
    const answer = await send('adblock.myFilters', {}, this.#transport);
    return { text: text(record(answer), 'text'), problems: parseProblems(answer) };
  }

  async saveMyFilters(value: string): Promise<FilterProblem[]> {
    return parseProblems(await send('adblock.saveMyFilters', { text: value }, this.#transport));
  }

  async checkFilters(value: string): Promise<FilterProblem[]> {
    return parseProblems(await send('adblock.checkFilters', { text: value }, this.#transport));
  }
}
