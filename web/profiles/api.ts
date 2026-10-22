/**
 * The profile manager's messages to the browser (005). Every answer is
 * checked before the page uses it.
 */
import { BridgeError, isRecord, send, type Transport } from '../shared/bridge.js';

export interface Profile {
  readonly id: string;
  readonly name: string;
  /** One of ProfilesState.colors. */
  readonly color: string;
  readonly isDefault: boolean;
  readonly openTabs: number;
}

export interface ProfilesState {
  readonly profiles: readonly Profile[];
  /** The colours a profile can have, in the order to offer them. */
  readonly colors: readonly string[];
  readonly showAtStartup: boolean;
}

function invalid(): BridgeError {
  return new BridgeError('The browser sent an answer that could not be read.');
}

function parseProfile(value: unknown): Profile {
  if (
    !isRecord(value) ||
    typeof value['id'] !== 'string' ||
    typeof value['name'] !== 'string' ||
    typeof value['color'] !== 'string' ||
    typeof value['isDefault'] !== 'boolean' ||
    typeof value['openTabs'] !== 'number' ||
    !Number.isInteger(value['openTabs']) ||
    value['openTabs'] < 0
  ) {
    throw invalid();
  }
  return {
    id: value['id'],
    name: value['name'],
    color: value['color'],
    isDefault: value['isDefault'],
    openTabs: value['openTabs'],
  };
}

export function parseState(value: unknown): ProfilesState {
  if (
    !isRecord(value) ||
    !Array.isArray(value['profiles']) ||
    !Array.isArray(value['colors']) ||
    typeof value['showAtStartup'] !== 'boolean'
  ) {
    throw invalid();
  }
  const colors: unknown[] = value['colors'];
  if (!colors.every((c): c is string => typeof c === 'string')) {
    throw invalid();
  }
  const list: unknown[] = value['profiles'];
  return {
    profiles: list.map(parseProfile),
    colors,
    showAtStartup: value['showAtStartup'],
  };
}

export interface ImportResult {
  /** The new profile's id; null if the user cancelled. */
  readonly id: string | null;
  /** What in the file was not imported, for the user. */
  readonly problems: readonly string[];
}

export function parseImportResult(value: unknown): ImportResult {
  if (!isRecord(value) || !Array.isArray(value['problems'])) {
    throw invalid();
  }
  const id = value['id'];
  const problems: unknown[] = value['problems'];
  if ((id !== null && typeof id !== 'string') ||
      !problems.every((p): p is string => typeof p === 'string')) {
    throw invalid();
  }
  return { id, problems };
}

export class ProfilesApi {
  readonly #transport: Transport | null;

  constructor(transport: Transport | null) {
    this.#transport = transport;
  }

  async state(): Promise<ProfilesState> {
    return parseState(await send('profiles.state', {}, this.#transport));
  }

  /** Resolves with the new profile's id. */
  async create(name: string, color: string): Promise<string> {
    const answer = await send('profiles.create', { name, color }, this.#transport);
    if (!isRecord(answer) || typeof answer['id'] !== 'string') {
      throw invalid();
    }
    return answer['id'];
  }

  async update(id: string, name: string, color: string): Promise<void> {
    await send('profiles.update', { id, name, color }, this.#transport);
  }

  async setDefault(id: string): Promise<void> {
    await send('profiles.setDefault', { id }, this.#transport);
  }

  async setShowAtStartup(show: boolean): Promise<void> {
    await send('profiles.setShowAtStartup', { show }, this.#transport);
  }

  /** Opens a window with a new tab in the profile. */
  async open(id: string): Promise<void> {
    await send('profiles.open', { id }, this.#transport);
  }

  /**
   * Asks where to save (natively) and exports the profile. Resolves with
   * false if the user cancelled.
   */
  async exportProfile(id: string): Promise<boolean> {
    const answer = await send('profiles.export', { id }, this.#transport);
    if (!isRecord(answer) || typeof answer['saved'] !== 'boolean') {
      throw invalid();
    }
    return answer['saved'];
  }

  /** Asks for an export file (natively) and imports it as a new profile. */
  async importProfile(): Promise<ImportResult> {
    return parseImportResult(await send('profiles.import', {}, this.#transport));
  }

  /**
   * Asks the user (in a native dialog) and deletes the profile. Resolves
   * with false if the user cancelled.
   */
  async remove(id: string): Promise<boolean> {
    const answer = await send('profiles.delete', { id }, this.#transport);
    if (!isRecord(answer) || typeof answer['deleted'] !== 'boolean') {
      throw invalid();
    }
    return answer['deleted'];
  }
}
