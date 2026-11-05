/**
 * The tools page's messages to the browser (006). The browser runs the
 * allowed Chrome DevTools Protocol commands on the dock's tab only.
 */
import { BridgeError, isRecord, send, type Transport } from '../shared/bridge.js';
import { parseDetails, parseSummary, type RequestDetails, type RequestSummary } from './requests_logic.js';
import type { PageRequest } from './security_logic.js';
import { parseRule, type Rule } from './rules_logic.js';
import { environmentMessage, parseEnvironmentState, type EnvironmentState, type TabEnvironment } from './environment_logic.js';

/** What the browser gives the security check (029). */
export interface SecurityAnswer {
  readonly url: string;
  readonly site: string;
  readonly document: RequestDetails | null;
  readonly siteRequests: RequestDetails[];
  readonly pageRequests: PageRequest[];
  readonly sessionCookies: string;
  readonly secretParameters: string;
  readonly hidden: string[];
  readonly canHide: boolean;
}

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

  /** The tab's service workers (all sites; the caller filters). */
  async serviceWorkers(): Promise<unknown> {
    return send('tools.serviceWorkers', {}, this.#transport);
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

  /** The tab's environment overrides (027) and the user agent presets. */
  async environment(): Promise<EnvironmentState> {
    return parseEnvironmentState(await send('tools.environment', {}, this.#transport));
  }

  /** Replaces the tab's overrides; rejects with what the browser did not accept. */
  async setEnvironment(environment: TabEnvironment): Promise<void> {
    await send('tools.setEnvironment', environmentMessage(environment), this.#transport);
  }

  /** Reloads the dock's tab. */
  async reloadTab(): Promise<void> {
    await send('tools.reloadTab', {}, this.#transport);
  }

  /** Text the page sent with "Decode with…" (once), and to which utility. */
  async takeUtilityInput(): Promise<{ utility: string; text: string } | null> {
    const answer = await send('tools.takeUtilityInput', {}, this.#transport);
    if (!isRecord(answer) || typeof answer['utility'] !== 'string' || typeof answer['text'] !== 'string') return null;
    return answer['utility'] === '' ? null : { utility: answer['utility'], text: answer['text'] };
  }

  /** The tab's recorded page load and requests, settings and hidden findings, for the security check. */
  async securityCheck(): Promise<SecurityAnswer> {
    const answer = await send('tools.securityCheck', {}, this.#transport);
    if (!isRecord(answer) || typeof answer['url'] !== 'string') throw invalid();
    const text = (key: string): string => (typeof answer[key] === 'string' ? answer[key] : '');
    const array = (key: string): unknown[] => {
      const value: unknown = answer[key];
      return Array.isArray(value) ? value : [];
    };
    return {
      url: answer['url'],
      site: text('site'),
      document: answer['document'] === null ? null : parseDetails(answer['document']),
      siteRequests: array('siteRequests').map(parseDetails).filter((r): r is RequestDetails => r !== null),
      pageRequests: array('pageRequests').flatMap((r) => isRecord(r) && typeof r['url'] === 'string'
        ? [{ url: r['url'], type: typeof r['type'] === 'string' ? r['type'] : '',
          error: typeof r['error'] === 'string' ? r['error'] : '' }] : []),
      sessionCookies: text('sessionCookies'),
      secretParameters: text('secretParameters'),
      hidden: array('hidden').filter((id): id is string => typeof id === 'string'),
      canHide: answer['canHide'] === true,
    };
  }

  /** The findings hidden for the page's site (the browser decides the site). */
  async setSecurityHidden(ids: readonly string[]): Promise<void> {
    await send('tools.setSecurityHidden', { ids }, this.#transport);
  }

  /** Runs a protocol command on the tab; resolves with its result object. */
  async protocol(method: string, params: Readonly<Record<string, unknown>> = {}): Promise<Record<string, unknown>> {
    const answer = await send('tools.protocol', { method, params }, this.#transport);
    return isRecord(answer) ? answer : {};
  }
}
