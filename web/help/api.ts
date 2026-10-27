/**
 * The help page's messages to the browser (016): the parts of help made
 * from the code. Every answer is checked before the page uses it.
 */
import { BridgeError, isRecord, send, type Transport } from '../shared/bridge.js';

export interface ShortcutRow {
  readonly id: string;
  readonly label: string;
  readonly setting: string;
  /** As macOS menus show them, e.g. "⇧⌘T". */
  readonly keys: readonly string[];
}

export interface SettingRow {
  readonly key: string;
  readonly label: string;
  readonly description: string;
  readonly section: string;
  readonly scope: 'global' | 'profile';
  readonly type: string;
  readonly defaultValue: string;
  readonly choices: ReadonlyArray<{ readonly value: string; readonly label: string }>;
}

export interface HelpState {
  readonly shortcuts: readonly ShortcutRow[];
  readonly settings: readonly SettingRow[];
}

function invalid(): BridgeError {
  return new BridgeError('The browser sent an answer that could not be read.');
}

function text(record: Record<string, unknown>, key: string): string {
  const value = record[key];
  if (typeof value !== 'string') throw invalid();
  return value;
}

function list(record: Record<string, unknown>, key: string): unknown[] {
  const value = record[key];
  if (!Array.isArray(value)) throw invalid();
  return value as unknown[];
}

export function parseState(value: unknown): HelpState {
  if (!isRecord(value)) throw invalid();
  const shortcuts = list(value, 'shortcuts').map((row): ShortcutRow => {
    if (!isRecord(row)) throw invalid();
    return {
      id: text(row, 'id'),
      label: text(row, 'label'),
      setting: text(row, 'setting'),
      keys: list(row, 'keys').map((key) => {
        if (typeof key !== 'string') throw invalid();
        return key;
      }),
    };
  });
  const settings = list(value, 'settings').map((row): SettingRow => {
    if (!isRecord(row)) throw invalid();
    const scope = text(row, 'scope');
    if (scope !== 'global' && scope !== 'profile') throw invalid();
    return {
      key: text(row, 'key'),
      label: text(row, 'label'),
      description: text(row, 'description'),
      section: text(row, 'section'),
      scope,
      type: text(row, 'type'),
      defaultValue: text(row, 'default'),
      choices: list(row, 'choices').map((choice) => {
        if (!isRecord(choice)) throw invalid();
        return { value: text(choice, 'value'), label: text(choice, 'label') };
      }),
    };
  });
  return { shortcuts, settings };
}

export async function loadState(transport: Transport | null): Promise<HelpState> {
  return parseState(await send('help.state', {}, transport));
}
