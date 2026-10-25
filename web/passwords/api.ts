/**
 * The passwords page's messages to the browser (008). Every answer is
 * checked before the page uses it. Secrets come only after the user
 * unlocked the page with Touch ID or the Mac's password.
 */
import { BridgeError, isRecord, send, type Transport } from '../shared/bridge.js';

export interface Profile {
  readonly id: string;
  readonly name: string;
  readonly color: string;
  /** Uses Bitwarden instead of the built-in passwords. */
  readonly bitwarden: boolean;
}

export interface PageState {
  readonly profiles: readonly Profile[];
  readonly currentProfile: string;
  /** Seconds the page stays unlocked; 0 when locked. */
  readonly unlockedFor: number;
  readonly breachCheck: boolean;
}

/** One saved login, without its secrets. */
export interface Entry {
  readonly profile: string;
  readonly id: number;
  readonly origin: string;
  readonly others: readonly string[];
  readonly username: string;
  readonly hasNote: boolean;
  readonly hasTotp: boolean;
  readonly created: number;
  readonly lastUsed: number;
  readonly changed: number;
  readonly timesUsed: number;
}

export interface Secrets {
  readonly password: string;
  readonly note: string;
  readonly totp: string;
  readonly code: string;
  readonly secondsLeft: number;
}

export interface NeverSaved {
  readonly profile: string;
  readonly origin: string;
}

export interface ImportSummary {
  readonly format: string;
  readonly added: number;
  readonly skipped: number;
  readonly conflicts: readonly { origin: string; username: string }[];
  readonly problems: readonly string[];
}

export interface CheckupEntry extends Entry {
  readonly password: string;
  /** Times seen in breaches (0: not found or not checked). */
  readonly breached: number;
}

export interface Checkup {
  readonly entries: readonly CheckupEntry[];
  /** Groups of indices into `entries` sharing one password. */
  readonly reused: readonly (readonly number[])[];
}

/** Thrown when the page must be unlocked first. */
export class LockedError extends BridgeError {
  override name = 'LockedError';
}

function invalid(): BridgeError {
  return new BridgeError('The browser sent an answer that could not be read.');
}

function record(value: unknown): Record<string, unknown> {
  if (!isRecord(value)) throw invalid();
  return value;
}

function list(value: unknown): unknown[] {
  if (!Array.isArray(value)) throw invalid();
  return value as unknown[];
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

function flag(item: Record<string, unknown>, key: string): boolean {
  const value = item[key];
  if (typeof value !== 'boolean') throw invalid();
  return value;
}

export function parseState(value: unknown): PageState {
  const answer = record(value);
  return {
    profiles: list(answer['profiles']).map((p) => {
      const item = record(p);
      return { id: text(item, 'id'), name: text(item, 'name'), color: text(item, 'color'), bitwarden: flag(item, 'bitwarden') };
    }),
    currentProfile: text(answer, 'currentProfile'),
    unlockedFor: number(answer, 'unlockedFor'),
    breachCheck: flag(answer, 'breachCheck'),
  };
}

export function parseEntry(value: unknown): Entry {
  const item = record(value);
  return {
    profile: text(item, 'profile'),
    id: number(item, 'id'),
    origin: text(item, 'origin'),
    others: list(item['others']).map((o) => {
      if (typeof o !== 'string') throw invalid();
      return o;
    }),
    username: text(item, 'username'),
    hasNote: flag(item, 'hasNote'),
    hasTotp: flag(item, 'hasTotp'),
    created: number(item, 'created'),
    lastUsed: number(item, 'lastUsed'),
    changed: number(item, 'changed'),
    timesUsed: number(item, 'timesUsed'),
  };
}

export function parseSecrets(value: unknown): Secrets {
  const item = record(value);
  return {
    password: text(item, 'password'),
    note: text(item, 'note'),
    totp: text(item, 'totp'),
    code: text(item, 'code'),
    secondsLeft: number(item, 'secondsLeft'),
  };
}

export function parseImport(value: unknown): ImportSummary | null {
  if (value === null) return null;
  const item = record(value);
  return {
    format: text(item, 'format'),
    added: number(item, 'added'),
    skipped: number(item, 'skipped'),
    conflicts: list(item['conflicts']).map((c) => {
      const row = record(c);
      return { origin: text(row, 'origin'), username: text(row, 'username') };
    }),
    problems: list(item['problems']).map((p) => {
      if (typeof p !== 'string') throw invalid();
      return p;
    }),
  };
}

export function parseCheckup(value: unknown): Checkup {
  const item = record(value);
  const entries = list(item['entries']).map((e) => {
    const row = record(e);
    return { ...parseEntry(row), password: text(row, 'password'), breached: number(row, 'breached') };
  });
  const reused = list(item['reused']).map((g) =>
    list(g).map((i) => {
      if (typeof i !== 'number' || !Number.isInteger(i) || i < 0 || i >= entries.length) throw invalid();
      return i;
    }),
  );
  return { entries, reused };
}

export class PasswordsApi {
  readonly #transport: Transport | null;

  constructor(transport: Transport | null) {
    this.#transport = transport;
  }

  async #send(type: string, args: Readonly<Record<string, unknown>> = {}): Promise<unknown> {
    try {
      return await send(type, args, this.#transport);
    } catch (error) {
      if (error instanceof BridgeError && error.message === 'Locked.') {
        throw new LockedError('Unlock the passwords first.');
      }
      throw error;
    }
  }

  async state(): Promise<PageState> {
    return parseState(await this.#send('passwords.state'));
  }

  async list(): Promise<Entry[]> {
    return list(await this.#send('passwords.list')).map(parseEntry);
  }

  /** Asks for Touch ID or the Mac's password; resolves with success. */
  async unlock(): Promise<boolean> {
    return (await this.#send('passwords.unlock')) === true;
  }

  async lock(): Promise<void> {
    await this.#send('passwords.lock');
  }

  async reveal(profile: string, id: number): Promise<Secrets> {
    return parseSecrets(await this.#send('passwords.reveal', { profile, id }));
  }

  async copy(profile: string, id: number, field: 'username' | 'password' | 'code'): Promise<void> {
    await this.#send('passwords.copy', { profile, id, field });
  }

  /** `id` 0 adds a new entry. */
  async save(entry: {
    profile: string;
    id: number;
    address: string;
    others: readonly string[];
    username: string;
    password: string;
    note: string;
    totp: string;
  }): Promise<void> {
    await this.#send('passwords.save', entry);
  }

  async remove(profile: string, ids: readonly number[]): Promise<void> {
    await this.#send('passwords.delete', { profile, ids });
  }

  async undo(): Promise<void> {
    await this.#send('passwords.undo');
  }

  async neverSaved(): Promise<NeverSaved[]> {
    return list(await this.#send('passwords.never')).map((n) => {
      const row = record(n);
      return { profile: text(row, 'profile'), origin: text(row, 'origin') };
    });
  }

  async allowSaving(profile: string, origin: string): Promise<void> {
    await this.#send('passwords.never.remove', { profile, origin });
  }

  /** Null when the user cancelled the file panel. */
  async importCsv(profile: string): Promise<ImportSummary | null> {
    return parseImport(await this.#send('passwords.import', { profile }));
  }

  async exportCsv(profile: string): Promise<boolean> {
    return (await this.#send('passwords.export', { profile })) === true;
  }

  async checkup(): Promise<Checkup> {
    return parseCheckup(await this.#send('passwords.checkup'));
  }
}
