/**
 * The settings page's site permissions messages (012). Every answer is
 * checked before the page uses it.
 */
import { BridgeError, isRecord, send, type Transport } from '../shared/bridge.js';

export type PermissionSetting = 'allow' | 'block' | 'ask';

export interface SitePermission {
  /** The kind's id, e.g. "media_stream_camera". */
  readonly id: string;
  readonly setting: 'allow' | 'block';
}

export interface PermissionSite {
  /** "https://example.com" */
  readonly origin: string;
  readonly permissions: readonly SitePermission[];
}

export interface PermissionKind {
  readonly id: string;
  readonly name: string;
}

export interface PermissionsState {
  readonly sites: readonly PermissionSite[];
  readonly kinds: readonly PermissionKind[];
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

export function parsePermissionsState(value: unknown): PermissionsState {
  const answer = record(value);
  return {
    sites: list(answer, 'sites').map((s) => {
      const site = record(s);
      return {
        origin: text(site, 'origin'),
        permissions: list(site, 'permissions').map((p) => {
          const item = record(p);
          const setting = item['setting'];
          if (setting !== 'allow' && setting !== 'block') throw invalid();
          return { id: text(item, 'id'), setting };
        }),
      };
    }),
    kinds: list(answer, 'kinds').map((k) => {
      const item = record(k);
      return { id: text(item, 'id'), name: text(item, 'name') };
    }),
  };
}

export class PermissionsApi {
  readonly #transport: Transport | null;

  constructor(transport: Transport | null) {
    this.#transport = transport;
  }

  /** The sites `profile` has allowed or blocked something for. */
  async state(profile: string): Promise<PermissionsState> {
    return parsePermissionsState(await send('permissions.state', { profile }, this.#transport));
  }

  /** "ask" removes the decision: the site asks again. */
  async set(profile: string, origin: string, id: string, setting: PermissionSetting): Promise<void> {
    await send('permissions.set', { profile, origin, id, setting }, this.#transport);
  }

  /** Every permission of the site back to "ask". */
  async reset(profile: string, origin: string): Promise<void> {
    await send('permissions.reset', { profile, origin }, this.#transport);
  }
}
