/** Settings page rules kept free of the DOM. */
import type { SettingItem } from './api.js';

export interface Section {
  readonly title: string;
  readonly items: readonly SettingItem[];
}

/** Items grouped by their section, in the order the sections first appear. */
export function sections(items: readonly SettingItem[]): Section[] {
  const result: { title: string; items: SettingItem[] }[] = [];
  for (const item of items) {
    let section = result.find((s) => s.title === item.section);
    if (section === undefined) {
      section = { title: item.section, items: [] };
      result.push(section);
    }
    section.items.push(item);
  }
  return result;
}

/**
 * A topic in the settings sidebar: the settings for all profiles and those
 * set per profile (shown with a tab per profile) on one page.
 */
export interface Topic {
  readonly title: string;
  readonly global: readonly SettingItem[];
  readonly profile: readonly SettingItem[];
}

/** Profile sections shown under a global topic of another name. */
const kTopicOfSection: Readonly<Record<string, string>> = { Profile: 'Profiles' };

/** The sidebar topic a section of settings belongs to. */
export function topicOfSection(section: string): string {
  return kTopicOfSection[section] ?? section;
}

/**
 * The topics, in the order of the global sections, then topics only a
 * profile has. Hidden items (see isShown) are left out.
 */
export function topics(global: readonly SettingItem[], profile: readonly SettingItem[]): Topic[] {
  const result: { title: string; global: SettingItem[]; profile: SettingItem[] }[] = [];
  const topicFor = (section: string): { global: SettingItem[]; profile: SettingItem[] } => {
    const title = topicOfSection(section);
    let topic = result.find((t) => t.title === title);
    if (topic === undefined) {
      topic = { title, global: [], profile: [] };
      result.push(topic);
    }
    return topic;
  };
  for (const item of global) {
    if (isShown(item, global)) topicFor(item.section).global.push(item);
  }
  for (const item of profile) {
    if (isShown(item, profile)) topicFor(item.section).profile.push(item);
  }
  return result;
}

/**
 * Whether an item is shown: the custom search address only matters when
 * "Custom" is the search engine.
 */
export function isShown(item: SettingItem, all: readonly SettingItem[]): boolean {
  if (item.key === 'search.custom_template') {
    const engine = all.find((i) => i.key === 'search.engine');
    return engine?.value === 'custom' || item.value !== '';
  }
  return true;
}

/** The parts of a keydown event a shortcut is recorded from. */
export interface KeyPress {
  readonly key: string;
  readonly code: string;
  readonly metaKey: boolean;
  readonly ctrlKey: boolean;
  readonly altKey: boolean;
  readonly shiftKey: boolean;
}

const kNamedKeys: Readonly<Record<string, string>> = {
  ArrowLeft: 'left',
  ArrowRight: 'right',
  ArrowUp: 'up',
  ArrowDown: 'down',
  Tab: 'tab',
  ' ': 'space',
  Enter: 'return',
  Escape: 'escape',
  Backspace: 'delete',
  Home: 'home',
  End: 'end',
  PageUp: 'pageup',
  PageDown: 'pagedown',
  ',': 'comma',
  '+': 'plus',
};

/** With Option, macOS types special characters: the US key is used instead. */
const kCodeKeys: Readonly<Record<string, string>> = {
  Minus: '-',
  Equal: '=',
  BracketLeft: '[',
  BracketRight: ']',
  Backslash: '\\',
  Semicolon: ';',
  Quote: "'",
  Comma: ',',
  Period: '.',
  Slash: '/',
  Backquote: '`',
};

/**
 * The shortcut a key press records, written as the settings file has it
 * ("ctrl+opt+shift+cmd+t"), or null if it is not a shortcut (no Cmd or Ctrl
 * and not F1 to F12, or only a modifier key): such keys edit the text.
 * Letters keep Shift; other characters are recorded as typed ("cmd+}" for
 * Cmd+Shift+]), as the browser matches them.
 */
export function shortcutFromKey(press: KeyPress): string | null {
  const functionKey = /^F([1-9]|1[0-2])$/.exec(press.key);
  let key: string | undefined;
  let shift = press.shiftKey;
  if (functionKey !== null) {
    key = press.key.toLowerCase();
  } else if (kNamedKeys[press.key] !== undefined) {
    key = kNamedKeys[press.key];
    if (key === 'comma' || key === 'plus') shift = false;
  } else if (/^Key[A-Z]$/.test(press.code)) {
    key = press.code.slice(3).toLowerCase();
  } else if (/^Digit[0-9]$/.test(press.code) && (press.altKey || !press.shiftKey)) {
    key = press.code.slice(5);
  } else if (press.altKey && kCodeKeys[press.code] !== undefined) {
    key = kCodeKeys[press.code];
  } else if (press.key.length === 1 && press.key > ' ' && press.key <= '~') {
    key = press.key;
    shift = false;  // Already in the character ("}" for Shift+]).
  }
  if (key === undefined) return null;
  if (functionKey === null && !press.metaKey && !press.ctrlKey) return null;
  const parts: string[] = [];
  if (press.ctrlKey) parts.push('ctrl');
  if (press.altKey) parts.push('opt');
  if (shift) parts.push('shift');
  if (press.metaKey) parts.push('cmd');
  parts.push(key);
  return parts.join('+');
}

/** A setting to show: its key, and the profile id for a profile's setting. */
export interface SettingTarget {
  readonly profile: string | null;
  readonly key: string;
}

/** Reads "#global:search.engine" or "#<profile id>:profile.color". */
export function parseSettingTarget(hash: string): SettingTarget | null {
  const match = /^#([a-z0-9-]+):([a-z0-9_.]+)$/.exec(hash);
  if (match === null) return null;
  const [, scope = '', key = ''] = match;
  return { profile: scope === 'global' ? null : scope, key };
}
