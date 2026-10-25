/**
 * The downloads page's messages to the browser (014). Every answer is
 * checked before the page uses it.
 */
import { BridgeError, isRecord, send, subscribe, type Subscriber, type Transport } from '../shared/bridge.js';

export interface Profile {
  readonly id: string;
  readonly name: string;
  readonly color: string;
}

export type DownloadState = 'in_progress' | 'paused' | 'complete' | 'failed' | 'cancelled';

export interface Download {
  readonly profile: string;
  readonly id: number;
  readonly url: string;
  readonly name: string;
  readonly site: string;
  readonly state: DownloadState;
  readonly status: string;
  readonly received: number;
  readonly total: number;
  readonly start: number;
  /** The file is still where it was saved. */
  readonly exists: boolean;
}

export type DownloadAction = 'open' | 'reveal' | 'copy' | 'pause' | 'resume' | 'cancel' | 'retry' | 'remove' | 'delete';

const kStates: readonly DownloadState[] = ['in_progress', 'paused', 'complete', 'failed', 'cancelled'];

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

export function parseDownload(value: unknown): Download {
  const item = record(value);
  const state = text(item, 'state');
  if (!kStates.includes(state as DownloadState)) throw invalid();
  const exists = item['exists'];
  if (typeof exists !== 'boolean') throw invalid();
  return {
    profile: text(item, 'profile'),
    id: number(item, 'id'),
    url: text(item, 'url'),
    name: text(item, 'name'),
    site: text(item, 'site'),
    state: state as DownloadState,
    status: text(item, 'status'),
    received: number(item, 'received'),
    total: number(item, 'total'),
    start: number(item, 'start'),
    exists,
  };
}

export class DownloadsApi {
  readonly #transport: Transport | null;
  readonly #subscriber: Subscriber | null;

  constructor(transport: Transport | null, subscriber: Subscriber | null) {
    this.#transport = transport;
    this.#subscriber = subscriber;
  }

  async state(): Promise<{ profiles: Profile[]; currentProfile: string }> {
    const answer = record(await send('downloads.state', {}, this.#transport));
    const list = answer['profiles'];
    if (!Array.isArray(list)) throw invalid();
    return {
      profiles: (list as unknown[]).map((p) => {
        const item = record(p);
        return { id: text(item, 'id'), name: text(item, 'name'), color: text(item, 'color') };
      }),
      currentProfile: text(answer, 'currentProfile'),
    };
  }

  /** `profile` '' for all profiles. */
  async list(profile: string): Promise<Download[]> {
    const answer = await send('downloads.list', { profile }, this.#transport);
    if (!Array.isArray(answer)) throw invalid();
    return (answer as unknown[]).map(parseDownload);
  }

  async action(download: Download, action: DownloadAction): Promise<void> {
    await send('downloads.action', { profile: download.profile, id: download.id, action }, this.#transport);
  }

  async clear(profile: string): Promise<void> {
    await send('downloads.clear', { profile }, this.#transport);
  }

  watch(changed: () => void): void {
    subscribe('downloads.watch', changed, this.#subscriber);
  }
}
