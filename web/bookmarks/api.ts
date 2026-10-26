/**
 * The bookmark manager's messages to the browser (011). Every answer is
 * checked before the page uses it.
 */
import { BridgeError, isRecord, send, subscribe, type Subscriber, type Transport } from '../shared/bridge.js';
import { parseIcons } from '../shared/icons.js';
import type { BookmarkNode, BookmarkTree } from './logic.js';

export interface Profile {
  readonly id: string;
  readonly name: string;
  readonly color: string;
}

export interface BookmarksState {
  readonly profiles: readonly Profile[];
  /** The manager tab's profile. */
  readonly currentProfile: string;
  readonly tree: BookmarkTree;
}

export interface ImportResult {
  readonly imported: boolean;
  readonly bookmarks: number;
  readonly folders: number;
}

/** Deeper trees are not read (the browser's limit is far below). */
const kMaxDepth = 1000;

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

function number(item: Record<string, unknown>, key: string): number {
  const value = item[key];
  if (typeof value !== 'number' || !Number.isFinite(value) || value < 0) throw invalid();
  return value;
}

export function parseNode(value: unknown, depth = 0): BookmarkNode {
  if (depth > kMaxDepth) throw invalid();
  const item = record(value);
  const type = item['type'];
  if (type !== 'folder' && type !== 'url') throw invalid();
  let children: BookmarkNode[] = [];
  if (type === 'folder') {
    const list = item['children'];
    if (!Array.isArray(list)) throw invalid();
    const items: unknown[] = list;
    children = items.map((child) => parseNode(child, depth + 1));
  }
  return {
    id: number(item, 'id'),
    type,
    name: text(item, 'name'),
    url: type === 'url' ? text(item, 'url') : '',
    profile: text(item, 'profile'),
    effective: text(item, 'effective'),
    added: number(item, 'added'),
    children,
  };
}

export function parseState(value: unknown): BookmarksState {
  const answer = record(value);
  const list = answer['profiles'];
  if (!Array.isArray(list)) throw invalid();
  const items: unknown[] = list;
  return {
    profiles: items.map((p) => {
      const item = record(p);
      return { id: text(item, 'id'), name: text(item, 'name'), color: text(item, 'color') };
    }),
    currentProfile: typeof answer['currentProfile'] === 'string' ? answer['currentProfile'] : '',
    tree: { bar: parseNode(answer['bar']), other: parseNode(answer['other']) },
  };
}

export function parseImport(value: unknown): ImportResult {
  const answer = record(value);
  const imported = answer['imported'];
  if (typeof imported !== 'boolean') throw invalid();
  return {
    imported,
    bookmarks: typeof answer['bookmarks'] === 'number' ? answer['bookmarks'] : 0,
    folders: typeof answer['folders'] === 'number' ? answer['folders'] : 0,
  };
}

export class BookmarksApi {
  readonly #transport: Transport | null;
  readonly #subscriber: Subscriber | null;

  constructor(transport: Transport | null, subscriber: Subscriber | null) {
    this.#transport = transport;
    this.#subscriber = subscriber;
  }

  async state(): Promise<BookmarksState> {
    return parseState(await send('bookmarks.state', {}, this.#transport));
  }

  /** Adds at `index` of `parent` (null: at the end); resolves with the id. */
  async add(item: {
    parent: number;
    index: number | null;
    folder: boolean;
    name: string;
    url: string;
    profile: string;
  }): Promise<number> {
    const { index, ...rest } = item;
    const answer = record(await send('bookmarks.add', index === null ? rest : { ...rest, index }, this.#transport));
    return number(answer, 'id');
  }

  /** `parent` moves the item to the end of that folder when it changed. */
  async edit(item: { id: number; name: string; url: string; profile: string; parent: number | null }): Promise<void> {
    const { parent, ...rest } = item;
    await send('bookmarks.edit', parent === null ? rest : { ...rest, parent }, this.#transport);
  }

  async move(ids: readonly number[], parent: number, index: number | null): Promise<void> {
    await send('bookmarks.move', index === null ? { ids, parent } : { ids, parent, index }, this.#transport);
  }

  async remove(ids: readonly number[]): Promise<void> {
    await send('bookmarks.remove', { ids }, this.#transport);
  }

  async undo(): Promise<boolean> {
    const answer = record(await send('bookmarks.undo', {}, this.#transport));
    return answer['undone'] === true;
  }

  async open(id: number, newTab: boolean): Promise<void> {
    await send('bookmarks.open', { id, newTab }, this.#transport);
  }

  async exportHtml(): Promise<boolean> {
    const answer = record(await send('bookmarks.export', {}, this.#transport));
    return answer['saved'] === true;
  }

  async importHtml(): Promise<ImportResult> {
    return parseImport(await send('bookmarks.import', {}, this.#transport));
  }

  /** Site icons (013) by bookmark id, as data: URLs. */
  async icons(): Promise<ReadonlyMap<string, string>> {
    return parseIcons(await send('bookmarks.icons', {}, this.#transport));
  }

  /** Calls `changed` whenever bookmarks change anywhere in the app. */
  watch(changed: () => void): void {
    subscribe('bookmarks.watch', changed, this.#subscriber);
  }
}
