/**
 * The settings page's link routing messages (010). Every answer is checked
 * before the page uses it.
 */
import { BridgeError, isRecord, send, type Transport } from '../shared/bridge.js';

export interface LinkRule {
  readonly pattern: string;
  /** Profile ids, in the order the picker offers them. */
  readonly profiles: readonly string[];
}

export interface LinkWrapper {
  readonly pattern: string;
  /** The query parameter with the real URL; '' for an opaque wrapper. */
  readonly parameter: string;
}

export interface BuiltInWrapper {
  readonly name: string;
  readonly hosts: string;
  readonly opaque: boolean;
}

export interface LinkProfile {
  readonly id: string;
  readonly name: string;
  readonly color: string;
}

export interface LinksState {
  /** False until the browser has started routing. */
  readonly ready: boolean;
  readonly rules: readonly LinkRule[];
  readonly wrappers: readonly LinkWrapper[];
  readonly builtins: readonly BuiltInWrapper[];
  readonly profiles: readonly LinkProfile[];
  /** Problems in the rules file (its invalid entries are left out). */
  readonly problems: readonly string[];
  readonly isDefaultBrowser: boolean;
}

/** A problem of one saved entry; `index` counts the entries sent. */
export interface SaveProblem {
  readonly kind: 'rule' | 'wrapper';
  readonly index: number;
  readonly message: string;
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

function texts(item: Record<string, unknown>, key: string): string[] {
  return list(item, key).map((value) => {
    if (typeof value !== 'string') throw invalid();
    return value;
  });
}

export function parseLinksState(value: unknown): LinksState {
  const answer = record(value);
  return {
    ready: answer['ready'] === true,
    rules: list(answer, 'rules').map((r) => {
      const item = record(r);
      return { pattern: text(item, 'pattern'), profiles: texts(item, 'profiles') };
    }),
    wrappers: list(answer, 'wrappers').map((w) => {
      const item = record(w);
      return { pattern: text(item, 'pattern'), parameter: text(item, 'parameter') };
    }),
    builtins: list(answer, 'builtins').map((b) => {
      const item = record(b);
      return { name: text(item, 'name'), hosts: text(item, 'hosts'), opaque: item['opaque'] === true };
    }),
    profiles: list(answer, 'profiles').map((p) => {
      const item = record(p);
      return { id: text(item, 'id'), name: text(item, 'name'), color: text(item, 'color') };
    }),
    problems: texts(answer, 'problems'),
    isDefaultBrowser: answer['isDefaultBrowser'] === true,
  };
}

export function parseSaveProblems(value: unknown): SaveProblem[] {
  return list(record(value), 'problems').map((p) => {
    const item = record(p);
    const kind = item['kind'];
    const index = item['index'];
    if ((kind !== 'rule' && kind !== 'wrapper') || typeof index !== 'number' || !Number.isInteger(index) || index < 0) {
      throw invalid();
    }
    return { kind, index, message: text(item, 'message') };
  });
}

export class LinksApi {
  readonly #transport: Transport | null;

  constructor(transport: Transport | null) {
    this.#transport = transport;
  }

  async state(): Promise<LinksState> {
    return parseLinksState(await send('links.state', {}, this.#transport));
  }

  /** Saves everything, or nothing while an entry has a problem. */
  async save(rules: readonly LinkRule[], wrappers: readonly LinkWrapper[]): Promise<SaveProblem[]> {
    return parseSaveProblems(await send('links.save', { rules, wrappers }, this.#transport));
  }

  async makeDefault(): Promise<void> {
    await send('links.makeDefault', {}, this.#transport);
  }
}
