/**
 * The open-source licences page (023): the data written at build time by
 * scripts/credits.mjs (credits/data.js), checked before use, and the search.
 */

export interface LicenceText {
  file: string;
  text: string;
}

export interface Component {
  name: string;
  version: string;
  licence: string;
  homepage: string;
  group: string;
  /** A page of further notices next to this one, e.g. "chromium". */
  notices: string;
  texts: LicenceText[];
}

export interface Credits {
  brouk: Component;
  components: Component[];
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function stringField(record: Record<string, unknown>, key: string): string | null {
  const value = record[key];
  return typeof value === 'string' ? value : null;
}

function parseComponent(value: unknown, texts: string[]): Component | null {
  if (!isRecord(value) || !Array.isArray(value['texts'])) return null;
  const name = stringField(value, 'name');
  const version = stringField(value, 'version');
  const licence = stringField(value, 'licence');
  const homepage = stringField(value, 'homepage');
  const group = stringField(value, 'group');
  if (name === null || version === null || licence === null || homepage === null || group === null) {
    return null;
  }
  const notices = stringField(value, 'notices') ?? '';
  // Only the plain page names next to this one.
  if (!/^[a-z-]*$/.test(notices)) return null;
  const parsed: LicenceText[] = [];
  for (const entry of value['texts']) {
    if (!isRecord(entry)) return null;
    const file = stringField(entry, 'file');
    const index = entry['text'];
    if (file === null || typeof index !== 'number') return null;
    const text = texts[index];
    if (text === undefined) return null;
    parsed.push({ file, text });
  }
  return { name, version, licence, homepage, group, notices, texts: parsed };
}

/** The generated data, or null if it is not what scripts/credits.mjs writes. */
export function parseCredits(value: unknown): Credits | null {
  if (!isRecord(value) || !Array.isArray(value['texts']) || !Array.isArray(value['components'])) {
    return null;
  }
  const texts: string[] = [];
  for (const text of value['texts']) {
    if (typeof text !== 'string') return null;
    texts.push(text);
  }
  const brouk = parseComponent(value['brouk'], texts);
  if (brouk === null) return null;
  const components: Component[] = [];
  for (const entry of value['components']) {
    const component = parseComponent(entry, texts);
    if (component === null) return null;
    components.push(component);
  }
  return { brouk, components };
}

/**
 * The components matching the search, in their groups (in the order the
 * groups first appear). Matches name and licence, ignoring case; every word
 * of the search must match.
 */
export function filterComponents(components: Component[], query: string): Map<string, Component[]> {
  const words = query.toLowerCase().split(/\s+/).filter((w) => w !== '');
  const groups = new Map<string, Component[]>();
  for (const component of components) {
    const haystack = `${component.name} ${component.licence}`.toLowerCase();
    if (!words.every((word) => haystack.includes(word))) continue;
    const list = groups.get(component.group) ?? [];
    list.push(component);
    groups.set(component.group, list);
  }
  return groups;
}

/** Only web addresses become links. */
export function safeHomepage(url: string): string | null {
  return /^https?:\/\/[^\s"<>]+$/.test(url) ? url : null;
}
