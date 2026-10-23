/**
 * The settings page's messages to the browser (004). Every answer is
 * checked before the page uses it.
 */
import { BridgeError, isRecord, send, type Transport } from '../shared/bridge.js';

export interface Choice {
  readonly value: string;
  readonly label: string;
}

export type SettingType = 'bool' | 'string' | 'choice';
export type SettingFormat = 'text' | 'urlTemplate' | 'folder' | 'profileId';

export interface SettingItem {
  readonly key: string;
  readonly type: SettingType;
  readonly format: SettingFormat;
  readonly section: string;
  readonly label: string;
  readonly description: string;
  /** Changing it needs a restart of Brouk. */
  readonly restart: boolean;
  readonly choices: readonly Choice[];
  readonly value: boolean | string;
}

export interface ProfileRef {
  readonly id: string;
  readonly name: string;
}

export interface SettingsState {
  /** Settings for all profiles. */
  readonly global: readonly SettingItem[];
  readonly profiles: readonly ProfileRef[];
  /** The settings of one profile. */
  readonly profile: { readonly id: string; readonly items: readonly SettingItem[] };
  /** Problems found in the settings files. */
  readonly problems: readonly string[];
}

export interface FileResult {
  /** False if the user cancelled. */
  readonly finished: boolean;
  /** What an import left out. */
  readonly problems: readonly string[];
}

const kTypes: readonly string[] = ['bool', 'string', 'choice'];
const kFormats: readonly string[] = ['text', 'urlTemplate', 'folder', 'profileId'];

function invalid(): BridgeError {
  return new BridgeError('The browser sent an answer that could not be read.');
}

function strings(value: unknown): string[] {
  if (!Array.isArray(value)) throw invalid();
  const list: unknown[] = value;
  if (!list.every((s): s is string => typeof s === 'string')) throw invalid();
  return list;
}

function parseChoice(value: unknown): Choice {
  if (!isRecord(value) || typeof value['value'] !== 'string' || typeof value['label'] !== 'string') {
    throw invalid();
  }
  return { value: value['value'], label: value['label'] };
}

export function parseItem(value: unknown): SettingItem {
  if (!isRecord(value)) throw invalid();
  const { key, type, format, section, label, description, restart, choices } = value;
  const current = value['value'];
  if (
    typeof key !== 'string' || typeof type !== 'string' || !kTypes.includes(type) ||
    typeof format !== 'string' || !kFormats.includes(format) ||
    typeof section !== 'string' || typeof label !== 'string' ||
    typeof description !== 'string' || typeof restart !== 'boolean' ||
    !Array.isArray(choices) ||
    (type === 'bool' ? typeof current !== 'boolean' : typeof current !== 'string')
  ) {
    throw invalid();
  }
  const list: unknown[] = choices;
  return {
    key,
    type: type as SettingType,
    format: format as SettingFormat,
    section,
    label,
    description,
    restart,
    choices: list.map(parseChoice),
    value: current as boolean | string,
  };
}

function items(value: unknown): SettingItem[] {
  if (!Array.isArray(value)) throw invalid();
  const list: unknown[] = value;
  return list.map(parseItem);
}

export function parseState(value: unknown): SettingsState {
  if (!isRecord(value) || !Array.isArray(value['profiles']) || !isRecord(value['profile'])) {
    throw invalid();
  }
  const profile = value['profile'];
  if (typeof profile['id'] !== 'string') throw invalid();
  const profiles: unknown[] = value['profiles'];
  return {
    global: items(value['global']),
    profiles: profiles.map((p) => {
      if (!isRecord(p) || typeof p['id'] !== 'string' || typeof p['name'] !== 'string') {
        throw invalid();
      }
      return { id: p['id'], name: p['name'] };
    }),
    profile: { id: profile['id'], items: items(profile['items']) },
    problems: strings(value['problems']),
  };
}

export function parseFileResult(value: unknown): FileResult {
  if (!isRecord(value) || typeof value['finished'] !== 'boolean') throw invalid();
  return { finished: value['finished'], problems: strings(value['problems']) };
}

export class SettingsApi {
  readonly #transport: Transport | null;

  constructor(transport: Transport | null) {
    this.#transport = transport;
  }

  /** All settings, with `profile`'s (the default profile's if null). */
  async state(profile: string | null): Promise<SettingsState> {
    const args = profile === null ? {} : { profile };
    return parseState(await send('settings.state', args, this.#transport));
  }

  /**
   * Changes one setting; `profile` is null for settings of all profiles.
   * Resolves with whether a restart is needed for it to apply.
   */
  async set(profile: string | null, key: string, value: boolean | string): Promise<boolean> {
    const args =
      profile === null ? { scope: 'global', key, value } : { scope: 'profile', profile, key, value };
    const answer = await send('settings.set', args, this.#transport);
    if (!isRecord(answer) || typeof answer['restart'] !== 'boolean') throw invalid();
    return answer['restart'];
  }

  async openFolder(): Promise<void> {
    await send('settings.openFolder', {}, this.#transport);
  }

  async exportSettings(): Promise<FileResult> {
    return parseFileResult(await send('settings.export', {}, this.#transport));
  }

  async importSettings(): Promise<FileResult> {
    return parseFileResult(await send('settings.import', {}, this.#transport));
  }
}
