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
