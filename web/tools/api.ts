/**
 * The tools page's messages to the browser (006). The browser runs the
 * allowed Chrome DevTools Protocol commands on the dock's tab only.
 */
import { BridgeError, isRecord, send, type Transport } from '../shared/bridge.js';

export class ToolsApi {
  readonly #transport: Transport | null;

  constructor(transport: Transport | null) {
    this.#transport = transport;
  }

  /** The URL of the tab the tools work on. */
  async pageUrl(): Promise<string> {
    const answer = await send('tools.page', {}, this.#transport);
    if (!isRecord(answer) || typeof answer['url'] !== 'string') {
      throw new BridgeError('The browser sent an answer that could not be read.');
    }
    return answer['url'];
  }

  /** Runs a protocol command on the tab; resolves with its result object. */
  async protocol(method: string, params: Readonly<Record<string, unknown>> = {}): Promise<Record<string, unknown>> {
    const answer = await send('tools.protocol', { method, params }, this.#transport);
    return isRecord(answer) ? answer : {};
  }
}
