/** JSON escaping, formatting and minifying (027). */
import { failed, ok, type Converted } from './util_common.js';

/** Text as a JSON string literal (with its quotes). */
export function escapeJson(text: string): string {
  return JSON.stringify(text);
}

/** A JSON string literal back to its text; the quotes may be left out. */
export function unescapeJson(literal: string): Converted {
  const trimmed = literal.trim();
  const quoted = trimmed.startsWith('"') && trimmed.endsWith('"') && trimmed.length >= 2 ? trimmed : `"${trimmed}"`;
  try {
    const value: unknown = JSON.parse(quoted);
    return typeof value === 'string' ? ok(value) : failed('Not a JSON string.');
  } catch (error) {
    return failed(`Not a valid JSON string: ${jsonError(error)}`);
  }
}

/** Pretty-printed (indent 2) or minified JSON, or where it is invalid. */
export function reformatJson(text: string, pretty: boolean): Converted {
  try {
    const value: unknown = JSON.parse(text);
    return ok(JSON.stringify(value, null, pretty ? 2 : undefined));
  } catch (error) {
    return failed(`Not valid JSON: ${jsonError(error)}`);
  }
}

function jsonError(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}
