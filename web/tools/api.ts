/**
 * The tools page's messages to the browser (006). The browser runs the
 * allowed Chrome DevTools Protocol commands on the dock's tab only.
 */
import { BridgeError, isRecord, send, type Transport } from '../shared/bridge.js';
import { parseDetails, parseSummary, type RequestDetails, type RequestSummary } from './requests_logic.js';
import { parseRule, type Rule } from './rules_logic.js';

export interface SaveFile {
  readonly name: string;
  readonly content: string;
}

function invalid(): BridgeError {
  return new BridgeError('The browser sent an answer that could not be read.');
}

export class ToolsApi {
  readonly #transport: Transport | null;

  constructor(transport: Transport | null) {
    this.#transport = transport;
  }

  /** The URL of the tab the tools work on. */
  async pageUrl(): Promise<string> {
    return (await this.page()).url;
  }

  /** The tab's URL and the secret header names setting. */
  async page(): Promise<{ url: string; secretHeaders: string }> {
    const answer = await send('tools.page', {}, this.#transport);
    if (!isRecord(answer) || typeof answer['url'] !== 'string') {
      throw invalid();
    }
    return {
      url: answer['url'],
      secretHeaders: typeof answer['secretHeaders'] === 'string' ? answer['secretHeaders'] : '',
    };
  }

  /**
   * The recorded requests, unless `version` is still current (then
   * `requests` is null).
   */
  async requests(version: number): Promise<{ version: number; requests: RequestSummary[] | null }> {
    const answer = await send('tools.requests', { version }, this.#transport);
    if (!isRecord(answer) || typeof answer['version'] !== 'number') throw invalid();
    const list: unknown = answer['requests'];
    return {
      version: answer['version'],
      requests: Array.isArray(list)
        ? list.map(parseSummary).filter((r): r is RequestSummary => r !== null)
        : null,
    };
  }

  async request(seq: number): Promise<RequestDetails> {
    const details = parseDetails(await send('tools.request', { seq }, this.#transport));
    if (details === null) throw invalid();
    return details;
  }

  async clearRequests(): Promise<void> {
    await send('tools.clearRequests', {}, this.#transport);
  }

  /** JWTs found in the recorded request and response headers. */
  async tokens(): Promise<{ token: string; where: string }[]> {
    const answer = await send('tools.tokens', {}, this.#transport);
    if (!isRecord(answer) || !Array.isArray(answer['tokens'])) throw invalid();
    const list: unknown[] = answer['tokens'];
    return list.flatMap((t) =>
      isRecord(t) && typeof t['token'] === 'string' && typeof t['where'] === 'string'
        ? [{ token: t['token'], where: t['where'] }] : []);
  }

  /** The request rules of the tab's profile. */
  async rules(): Promise<{ paused: boolean; rules: Rule[]; problems: string[]; profile: string }> {
    const answer = await send('tools.rules', {}, this.#transport);
    if (!isRecord(answer) || !Array.isArray(answer['rules']) || !Array.isArray(answer['problems'])) throw invalid();
    const rules: unknown[] = answer['rules'];
    const problems: unknown[] = answer['problems'];
    return {
      paused: answer['paused'] === true,
      rules: rules.map(parseRule).filter((r): r is Rule => r !== null),
      problems: problems.filter((p): p is string => typeof p === 'string'),
      profile: typeof answer['profile'] === 'string' ? answer['profile'] : '',
    };
  }

  async setRules(paused: boolean, rules: readonly Rule[]): Promise<void> {
    await send('tools.setRules', { paused, rules }, this.#transport);
  }

  /** Asks where to save and saves; resolves with false if cancelled. */
  async save(main: SaveFile, extra: SaveFile | null): Promise<boolean> {
    const answer = await send('tools.save', { ...main, ...(extra ? { extra } : {}) }, this.#transport);
    if (!isRecord(answer) || typeof answer['saved'] !== 'boolean') throw invalid();
    return answer['saved'];
  }

  /** Runs a protocol command on the tab; resolves with its result object. */
  async protocol(method: string, params: Readonly<Record<string, unknown>> = {}): Promise<Record<string, unknown>> {
    const answer = await send('tools.protocol', { method, params }, this.#transport);
    return isRecord(answer) ? answer : {};
  }
}
