/**
 * The profile manager's messages to the browser (005). Every answer is
 * checked before the page uses it.
 */
import { BridgeError, isRecord, send, type Transport } from '../shared/bridge.js';

/** A pattern whose links open in the profile (010). */
export interface ProfileLink {
  readonly pattern: string;
  /** Other profiles the same rule offers in the picker. */
  readonly sharedWith: readonly string[];
}

export interface Profile {
  readonly id: string;
  readonly name: string;
  /** One of ProfilesState.colors. */
  readonly color: string;
  readonly isDefault: boolean;
  readonly openTabs: number;
  readonly links: readonly ProfileLink[];
}

export interface ProfilesState {
  readonly profiles: readonly Profile[];
  /** The colours a profile can have, in the order to offer them. */
  readonly colors: readonly string[];
  readonly showAtStartup: boolean;
  /** False until the browser has started link routing. */
  readonly linksReady: boolean;
}

/** A pattern the browser did not accept; `index` is its position. */
export interface LinkProblem {
  readonly index: number;
  readonly message: string;
}

function invalid(): BridgeError {
  return new BridgeError('The browser sent an answer that could not be read.');
}

function parseLink(value: unknown): ProfileLink {
  if (!isRecord(value) || typeof value['pattern'] !== 'string' || !Array.isArray(value['sharedWith'])) {
    throw invalid();
  }
  const shared: unknown[] = value['sharedWith'];
  if (!shared.every((s): s is string => typeof s === 'string')) {
    throw invalid();
  }
  return { pattern: value['pattern'], sharedWith: shared };
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
    value['openTabs'] < 0 ||
    !Array.isArray(value['links'])
  ) {
    throw invalid();
  }
  const links: unknown[] = value['links'];
  return {
    id: value['id'],
    name: value['name'],
    color: value['color'],
    isDefault: value['isDefault'],
    openTabs: value['openTabs'],
    links: links.map(parseLink),
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
    linksReady: value['linksReady'] === true,
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

  /**
   * Makes `patterns` the profile's links (010). Nothing is saved while one
   * is not valid; resolves with those problems.
   */
  async setLinks(id: string, patterns: readonly string[]): Promise<LinkProblem[]> {
    const answer = await send('profiles.setLinks', { id, patterns }, this.#transport);
    if (!isRecord(answer) || !Array.isArray(answer['problems'])) {
      throw invalid();
    }
    const problems: unknown[] = answer['problems'];
    return problems.map((p) => {
      if (!isRecord(p) || typeof p['index'] !== 'number' || !Number.isInteger(p['index']) ||
          typeof p['message'] !== 'string') {
        throw invalid();
      }
      return { index: p['index'], message: p['message'] };
    });
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
