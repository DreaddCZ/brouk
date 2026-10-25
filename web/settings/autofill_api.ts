/**
 * The settings page's autofill messages (009). Every answer is checked
 * before the page uses it.
 */
import { BridgeError, isRecord, send, type Transport } from '../shared/bridge.js';

export interface AutofillEntryRow {
  readonly id: number;
  readonly label: string;
  /** By field name ("first_name", "ico", …); only fields with a value. */
  readonly values: Readonly<Record<string, string>>;
  /** Validation problems by field name. */
  readonly problems: Readonly<Record<string, string>>;
  readonly originSite: string;
  readonly created: number;
  readonly lastUsed: number;
  readonly timesUsed: number;
}

export interface CustomFieldRow {
  readonly id: number;
  readonly name: string;
  readonly value: string;
  readonly labelWords: readonly string[];
  readonly lastUsed: number;
}

export interface AutofillState {
  readonly loaded: boolean;
  readonly error: string;
  readonly country: string;
  readonly entries: readonly AutofillEntryRow[];
  readonly custom: readonly CustomFieldRow[];
  readonly canUndo: boolean;
}

export interface AutofillImport {
  readonly added: number;
  readonly merged: number;
  readonly skipped: number;
  readonly customAdded: number;
  readonly customSkipped: number;
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

function strings(value: unknown): Record<string, string> {
  const item = record(value);
  const out: Record<string, string> = {};
  for (const [key, v] of Object.entries(item)) {
    if (typeof v !== 'string') throw invalid();
    out[key] = v;
  }
  return out;
}

export function parseAutofillState(value: unknown): AutofillState {
  const answer = record(value);
  return {
    loaded: answer['loaded'] === true,
    error: text(answer, 'error'),
    country: text(answer, 'country'),
    entries: list(answer['entries']).map((e) => {
      const row = record(e);
      return {
        id: number(row, 'id'),
        label: text(row, 'label'),
        values: strings(row['values']),
        problems: strings(row['problems']),
        originSite: text(row, 'originSite'),
        created: number(row, 'created'),
        lastUsed: number(row, 'lastUsed'),
        timesUsed: number(row, 'timesUsed'),
      };
    }),
    custom: list(answer['custom']).map((c) => {
      const row = record(c);
      return {
        id: number(row, 'id'),
        name: text(row, 'name'),
        value: text(row, 'value'),
        labelWords: list(row['labelWords']).map((w) => {
          if (typeof w !== 'string') throw invalid();
          return w;
        }),
        lastUsed: number(row, 'lastUsed'),
      };
    }),
    canUndo: answer['canUndo'] === true,
  };
}

export class AutofillApi {
  readonly #transport: Transport | null;

  constructor(transport: Transport | null) {
    this.#transport = transport;
  }

  async state(): Promise<AutofillState> {
    return parseAutofillState(await send('autofill.state', {}, this.#transport));
  }

  /** id 0 adds a new entry. */
  async saveEntry(id: number, label: string, values: Readonly<Record<string, string>>): Promise<void> {
    await send('autofill.saveEntry', { id, label, values }, this.#transport);
  }

  async deleteEntries(ids: readonly number[]): Promise<void> {
    await send('autofill.deleteEntries', { ids }, this.#transport);
  }

  async saveCustom(id: number, name: string, value: string, labelWords: readonly string[]): Promise<void> {
    await send('autofill.saveCustom', { id, name, value, labelWords }, this.#transport);
  }

  async deleteCustom(ids: readonly number[]): Promise<void> {
    await send('autofill.deleteCustom', { ids }, this.#transport);
  }

  async undo(): Promise<void> {
    await send('autofill.undo', {}, this.#transport);
  }

  /** Groups of entry ids that look like duplicates. */
  async duplicates(): Promise<number[][]> {
    return list(await send('autofill.duplicates', {}, this.#transport)).map((g) =>
      list(g).map((id) => {
        if (typeof id !== 'number') throw invalid();
        return id;
      }));
  }

  /** Keeps `id` with the chosen values and removes the rest of the group. */
  async merge(id: number, label: string, values: Readonly<Record<string, string>>, remove: readonly number[]): Promise<void> {
    await send('autofill.merge', { id, label, values, remove }, this.#transport);
  }

  async exportJson(): Promise<boolean> {
    return (await send('autofill.export', {}, this.#transport)) === true;
  }

  async importJson(): Promise<AutofillImport | null> {
    const answer = await send('autofill.import', {}, this.#transport);
    if (answer === null) return null;
    const row = record(answer);
    return {
      added: number(row, 'added'),
      merged: number(row, 'merged'),
      skipped: number(row, 'skipped'),
      customAdded: number(row, 'customAdded'),
      customSkipped: number(row, 'customSkipped'),
    };
  }
}
