/**
 * Runs the Utilities' regular expressions (027) off the page, so the page
 * can stop one that takes too long.
 */
import { runRegex } from './util_regex.js';

interface Job {
  readonly pattern: string;
  readonly flags: string;
  readonly text: string;
  readonly replacement: string | null;
}

// A worker's global scope (the DOM library types it as a window).
const scope = globalThis as unknown as {
  addEventListener(type: 'message', listener: (event: MessageEvent<Job>) => void): void;
  postMessage(message: unknown): void;
};

scope.addEventListener('message', (event) => {
  const job = event.data;
  scope.postMessage(runRegex(job.pattern, job.flags, job.text, job.replacement));
});
