/**
 * The only way an internal page talks to the browser (CLAUDE.md). A message
 * is a JSON object with a "type" and its arguments; the browser checks every
 * message and answers with JSON, or fails with a message for the user.
 */

/** Arguments of CEF's message router function (window.cefQuery). */
interface CefQueryOptions {
  request: string;
  persistent: boolean;
  onSuccess: (response: string) => void;
  onFailure: (code: number, message: string) => void;
}

export class BridgeError extends Error {
  override name = 'BridgeError';
}

/** Sends a request and resolves with the browser's answer. */
export type Transport = (request: string) => Promise<string>;

/**
 * The transport of a page shown by Brouk; null elsewhere (the browser adds
 * cefQuery to internal pages only).
 */
export function cefTransport(): Transport | null {
  const query: unknown = Reflect.get(globalThis, 'cefQuery');
  if (typeof query !== 'function') {
    return null;
  }
  const call = query as (options: CefQueryOptions) => number;
  return (request) =>
    new Promise<string>((resolve, reject) => {
      call({
        request,
        persistent: false,
        onSuccess: resolve,
        onFailure: (_code, message) => reject(new BridgeError(message)),
      });
    });
}

/** Sends one message; resolves with the parsed answer (null for none). */
export async function send(
  type: string,
  args: Readonly<Record<string, unknown>>,
  transport: Transport | null,
): Promise<unknown> {
  if (transport === null) {
    throw new BridgeError('This page works only inside Brouk.');
  }
  const answer = await transport(JSON.stringify({ ...args, type }));
  if (answer === '') {
    return null;
  }
  try {
    return JSON.parse(answer) as unknown;
  } catch {
    throw new BridgeError('The browser sent an answer that could not be read.');
  }
}

/**
 * Starts a lasting request: `onMessage` gets each message the browser sends
 * for it (e.g. "something changed"), until the page goes away.
 */
export type Subscriber = (request: string, onMessage: (message: string) => void) => void;

/** The subscriber of a page shown by Brouk; null elsewhere. */
export function cefSubscriber(): Subscriber | null {
  const query: unknown = Reflect.get(globalThis, 'cefQuery');
  if (typeof query !== 'function') {
    return null;
  }
  const call = query as (options: CefQueryOptions) => number;
  return (request, onMessage) => {
    call({ request, persistent: true, onSuccess: onMessage, onFailure: () => {} });
  };
}

/** Subscribes to messages of `type`; does nothing outside Brouk. */
export function subscribe(type: string, onMessage: () => void, subscriber: Subscriber | null): void {
  subscriber?.(JSON.stringify({ type }), () => onMessage());
}

export function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}
