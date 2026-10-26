/**
 * Site icons that come with an answer from the browser (013): a map of
 * keys to PNG data: URLs, which rows name by key. Anything else is dropped,
 * so a row without a usable icon simply shows the generic one.
 */
import { isRecord } from './bridge.js';

const kPngPrefix = 'data:image/png;base64,';
const kBase64 = /^[A-Za-z0-9+/]+={0,2}$/;

/** The answer's `icons` map; empty when it has none. */
export function parseIcons(answer: unknown): ReadonlyMap<string, string> {
  const icons = new Map<string, string>();
  if (!isRecord(answer) || !isRecord(answer['icons'])) return icons;
  for (const [key, value] of Object.entries(answer['icons'])) {
    if (typeof value === 'string' && value.startsWith(kPngPrefix) && kBase64.test(value.slice(kPngPrefix.length))) {
      icons.set(key, value);
    }
  }
  return icons;
}

/** `{ icon }` for a row whose `icon` key names a known icon, else `{}`. */
export function iconOf(row: Record<string, unknown>, icons: ReadonlyMap<string, string>): { icon?: string } {
  const key = row['icon'];
  const icon = typeof key === 'string' ? icons.get(key) : undefined;
  return icon === undefined ? {} : { icon };
}
