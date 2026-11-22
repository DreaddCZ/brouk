/**
 * Help for the platform it runs on (016, issue #65): the help sources are
 * written once for macOS and Windows with a few markers, and the build
 * (web/help/build.mjs) keeps what fits the platform it builds for:
 *
 *   {mac}…{/mac}, {win}…{/win}   text for one platform only (inline or
 *                                 around whole paragraphs)
 *   {key:mod+shift+t}             a key combination: ⇧⌘T on macOS,
 *                                 Ctrl+Shift+T on Windows
 *   {shortcut:new_tab}            the user's current shortcut for an action
 *                                 (src/core/shortcuts.cc), filled in by the
 *                                 page from the browser
 */

export type Platform = 'mac' | 'win';

/** The platform help is built for: BROUK_HELP_PLATFORM, else this OS. */
export function buildPlatform(env: string | undefined, nodePlatform: string): Platform {
  if (env === 'mac' || env === 'win') return env;
  return nodePlatform === 'win32' ? 'win' : 'mac';
}

// macOS shows modifiers as symbols in this order (⌃⌥⇧⌘), Windows as words
// joined by "+" (Win+Ctrl+Alt+Shift), as their menus do.
const kModifiers: ReadonlyArray<{ name: string; mac: string; win: string }> = [
  { name: 'win', mac: '⌘', win: 'Win' }, // The Windows key, for Windows text.
  { name: 'ctrl', mac: '⌃', win: 'Ctrl' },
  { name: 'alt', mac: '⌥', win: 'Alt' },
  { name: 'shift', mac: '⇧', win: 'Shift' },
  { name: 'mod', mac: '⌘', win: 'Ctrl' },
];

const kWindowsOrder = ['win', 'ctrl', 'mod', 'alt', 'shift'];

const kKeys: Readonly<Record<string, { mac: string; win: string }>> = {
  backspace: { mac: '⌫', win: 'Backspace' },
  delete: { mac: '⌦', win: 'Del' },
  esc: { mac: 'Esc', win: 'Esc' },
  return: { mac: 'Return', win: 'Enter' },
  tab: { mac: '⇥', win: 'Tab' },
  space: { mac: 'Space', win: 'Space' },
  left: { mac: '←', win: 'Left' },
  right: { mac: '→', win: 'Right' },
  up: { mac: '↑', win: 'Up' },
  down: { mac: '↓', win: 'Down' },
  plus: { mac: '+', win: 'Plus' },
  minus: { mac: '−', win: '-' },
  comma: { mac: ',', win: ',' },
};

/**
 * "mod+shift+t" as the platform writes it, or null if it is not a key
 * combination: modifiers (mod = ⌘ / Ctrl, ctrl, alt, shift, win) and at most one
 * key, joined by "+". A modifier alone ("mod") is allowed, for "⌘-click".
 */
export function formatKeys(combination: string, platform: Platform): string | null {
  const parts = combination.toLowerCase().split('+');
  if (parts.some((part) => part === '')) return null;
  const used = new Set<string>();
  let key: string | null = null;
  for (const part of parts) {
    if (kModifiers.some((m) => m.name === part)) {
      if (used.has(part) || key !== null) return null;
      used.add(part);
    } else {
      if (key !== null) return null;
      key = part;
    }
  }
  let keyText = '';
  if (key !== null) {
    const named = kKeys[key];
    if (named !== undefined) keyText = named[platform];
    else if (/^f([1-9]|1[0-2])$/.test(key)) keyText = key.toUpperCase();
    else if (key.length === 1 && key > ' ' && key <= '~') keyText = key.toUpperCase();
    else return null;
  }
  const modifiers: string[] = [];
  const order = platform === 'mac' ? kModifiers : kWindowsOrder.flatMap((name) => kModifiers.filter((m) => m.name === name));
  for (const modifier of order) {
    if (!used.has(modifier.name)) continue;
    const text = modifier[platform];
    if (!modifiers.includes(text)) modifiers.push(text); // ctrl+mod on Windows.
  }
  if (platform === 'mac') return modifiers.join('') + keyText;
  return [...modifiers, ...(keyText === '' ? [] : [keyText])].join('+');
}

export interface PlatformTextResult {
  readonly text: string;
  /** Markers that could not be read; the build fails on any. */
  readonly problems: readonly string[];
  /** The action ids of {shortcut:…}, checked by the build. */
  readonly actions: readonly string[];
}

function escapeHtml(text: string): string {
  return text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

/** Applies the markers above to one help source for `platform`. */
export function applyPlatform(source: string, platform: Platform): PlatformTextResult {
  const problems: string[] = [];
  const actions: string[] = [];
  // Each {mac} or {win} closes before the next marker opens: no nesting.
  let open: string | null = null;
  for (const [marker, slash, name] of source.matchAll(/\{(\/?)(mac|win)\}/g)) {
    if (slash === '' && open === null) open = name ?? null;
    else if (slash === '/' && open === name) open = null;
    else problems.push(`unmatched ${marker}`);
  }
  if (open !== null) problems.push(`unmatched {${open}}`);
  const other = platform === 'mac' ? 'win' : 'mac';
  let text = source
    .replace(new RegExp(`\\{${other}\\}[\\s\\S]*?\\{/${other}\\}`, 'g'), '')
    .replace(new RegExp(`\\{/?${platform}\\}`, 'g'), '');
  text = text.replace(/\{key:([^}\s]*)\}/g, (marker, combination: string) => {
    const keys = formatKeys(combination, platform);
    if (keys === null) {
      problems.push(`${marker} is not a key combination`);
      return marker;
    }
    return `<kbd>${escapeHtml(keys)}</kbd>`;
  });
  text = text.replace(/\{shortcut:([^}\s]*)\}/g, (marker, id: string) => {
    if (!/^[a-z0-9_]+$/.test(id)) {
      problems.push(`${marker} is not an action id`);
      return marker;
    }
    actions.push(id);
    return `<kbd class="shortcut" data-shortcut="${id}"></kbd>`;
  });
  // Blank lines left where a block for the other platform was.
  text = text.replace(/\n{3,}/g, '\n\n');
  return { text, problems, actions };
}
