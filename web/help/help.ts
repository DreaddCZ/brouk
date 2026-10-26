/**
 * The help page (016), brouk://help in its own window. The text is built
 * from web/help/content/*.md at build time; the keyboard shortcuts and the
 * settings reference come from the browser, so they show the user's
 * current shortcuts and every setting there is.
 */
import { cefTransport } from '../shared/bridge.js';
import { byId, element } from '../shared/dom.js';
import { settingsSectionAnchor } from '../shared/help.js';
import { loadState, type HelpState, type SettingRow } from './api.js';
import { matchesAll, matchRanges, queryWords } from './logic.js';

const content = byId<HTMLElement>('content');
const toc = byId<HTMLElement>('toc');
const search = byId<HTMLInputElement>('search');
const status = byId<HTMLParagraphElement>('search-status');
const tocLinks = new Map<string, HTMLAnchorElement>();

// ---- Generated sections ----

function keysCell(keys: readonly string[]): HTMLTableCellElement {
  const cell = element('td');
  if (keys.length === 0) {
    cell.append(element('span', { className: 'muted', text: 'none' }));
  }
  keys.forEach((key, index) => {
    if (index > 0) cell.append(' ');
    cell.append(element('kbd', { text: key }));
  });
  return cell;
}

function table(headers: readonly string[]): { table: HTMLTableElement; body: HTMLTableSectionElement } {
  const node = element('table');
  const head = element('thead');
  const row = element('tr');
  for (const header of headers) row.append(element('th', { text: header }));
  head.append(row);
  const body = element('tbody');
  node.append(head, body);
  return { table: node, body };
}

function renderShortcuts(state: HelpState): void {
  for (const target of document.querySelectorAll<HTMLElement>('[data-generated="shortcuts"]')) {
    const { table: node, body } = table(['Action', 'Shortcut', 'Setting']);
    for (const shortcut of state.shortcuts) {
      const row = element('tr');
      row.append(element('td', { text: shortcut.label }), keysCell(shortcut.keys));
      const setting = element('td');
      setting.append(element('code', { text: shortcut.setting }));
      row.append(setting);
      body.append(row);
    }
    target.replaceChildren(node);
  }
}

function defaultText(setting: SettingRow): string {
  if (setting.type === 'on/off') return setting.defaultValue === 'true' ? 'on' : 'off';
  if (setting.defaultValue === '') return 'empty';
  const choice = setting.choices.find((c) => c.value === setting.defaultValue);
  return choice === undefined ? setting.defaultValue : `${choice.label} (${choice.value})`;
}

function renderSettings(state: HelpState): void {
  for (const target of document.querySelectorAll<HTMLElement>('[data-generated="settings"]')) {
    const nodes: HTMLElement[] = [];
    const sections = [...new Set(state.settings.map((s) => s.section))];
    for (const section of sections) {
      const heading = element('h3', { text: section });
      heading.id = settingsSectionAnchor(section);
      const { table: node, body } = table(['Setting', 'Default', 'Scope']);
      for (const setting of state.settings.filter((s) => s.section === section)) {
        const row = element('tr');
        const name = element('td');
        name.append(element('strong', { text: setting.label }), element('br'), element('code', { text: setting.key }));
        if (setting.description !== '') {
          name.append(element('br'), element('span', { className: 'muted', text: setting.description }));
        }
        if (setting.choices.length > 0) {
          const values = setting.choices.map((c) => `${c.value} (${c.label})`).join(', ');
          name.append(element('br'), element('span', { className: 'muted', text: `Values: ${values}` }));
        }
        const scope = element('td', {
          className: 'scope',
          text: setting.scope === 'global' ? 'all profiles' : 'per profile',
        });
        row.append(name, element('td', { text: defaultText(setting) }), scope);
        body.append(row);
      }
      nodes.push(heading, node);
    }
    target.replaceChildren(...nodes);
  }
}

async function refreshGenerated(): Promise<void> {
  try {
    const state = await loadState(cefTransport());
    renderShortcuts(state);
    renderSettings(state);
  } catch {
    for (const target of document.querySelectorAll<HTMLElement>('[data-generated]')) {
      target.replaceChildren(element('p', { className: 'muted', text: 'Shown when help is opened in Brouk.' }));
    }
  }
  buildToc();
  applySearch();
}

// ---- Table of contents ----

function headings(): HTMLElement[] {
  return [...content.querySelectorAll<HTMLElement>('h2[id], h3[id]')];
}

function buildToc(): void {
  toc.replaceChildren();
  tocLinks.clear();
  for (const heading of headings()) {
    const link = element('a', { text: heading.textContent ?? '' });
    link.href = `#${heading.id}`;
    if (heading.tagName === 'H3') link.className = 'sub';
    link.addEventListener('click', (event) => {
      event.preventDefault();
      showSection(heading.id);
    });
    tocLinks.set(heading.id, link);
    toc.append(link);
  }
  updateCurrent();
}

/** Marks the section in view (the last heading above the top third). */
function updateCurrent(): void {
  const limit = content.getBoundingClientRect().top + content.clientHeight / 3;
  let current: string | null = null;
  for (const heading of headings()) {
    if (heading.offsetParent === null) continue; // Filtered out.
    if (heading.getBoundingClientRect().top <= limit) current = heading.id;
  }
  current ??= headings().find((h) => h.offsetParent !== null)?.id ?? null;
  for (const [id, link] of tocLinks) link.classList.toggle('current', id === current);
  tocLinks.get(current ?? '')?.scrollIntoView({ block: 'nearest' });
}

// ---- Search ----

function clearMarks(): void {
  for (const mark of content.querySelectorAll('mark')) {
    mark.replaceWith(document.createTextNode(mark.textContent ?? ''));
  }
  content.normalize();
}

function markWords(root: HTMLElement, words: readonly string[]): void {
  const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
  const nodes: Text[] = [];
  for (let node = walker.nextNode(); node !== null; node = walker.nextNode()) nodes.push(node as Text);
  for (const node of nodes) {
    const text = node.data;
    const ranges = matchRanges(text, words);
    if (ranges.length === 0) continue;
    const parts: Node[] = [];
    let at = 0;
    for (const [start, end] of ranges) {
      if (start > at) parts.push(document.createTextNode(text.slice(at, start)));
      parts.push(element('mark', { text: text.slice(start, end) }));
      at = end;
    }
    if (at < text.length) parts.push(document.createTextNode(text.slice(at)));
    node.replaceWith(...parts);
  }
}

function applySearch(): void {
  clearMarks();
  const words = queryWords(search.value);
  const all = [...content.querySelectorAll<HTMLElement>('.help-section')];
  let shown = 0;
  for (const section of all) {
    const match = matchesAll(section.textContent ?? '', words);
    section.hidden = !match;
    if (match) {
      shown++;
      if (words.length > 0) markWords(section, words);
    }
  }
  for (const [id, link] of tocLinks) {
    link.hidden = document.getElementById(id)?.closest<HTMLElement>('.help-section')?.hidden ?? false;
  }
  status.textContent =
    words.length === 0 ? '' : shown === 0 ? 'Nothing found' : `${shown} of ${all.length} sections`;
  updateCurrent();
}

// ---- Opening at a section ----

/** Scrolls to a section and highlights it briefly (context "?" links). */
function showSection(id: string): void {
  let target = document.getElementById(id);
  if (target === null) return;
  if (target.closest<HTMLElement>('.help-section')?.hidden === true) {
    search.value = '';
    applySearch();
    target = document.getElementById(id);
    if (target === null) return;
  }
  if (location.hash !== `#${id}`) history.replaceState(null, '', `#${id}`);
  target.scrollIntoView({ block: 'start' });
  target.classList.remove('flash');
  void target.offsetWidth; // Restart the animation.
  target.classList.add('flash');
  updateCurrent();
}

function showHash(): void {
  const id = decodeURIComponent(location.hash.slice(1));
  if (id !== '') showSection(id);
}

// The browser calls this when help is opened again at a section.
Reflect.set(globalThis, 'showHelpSection', (id: unknown) => {
  if (typeof id === 'string') showSection(id);
});

search.addEventListener('input', applySearch);
search.addEventListener('keydown', (event) => {
  if (event.key === 'Escape') {
    search.value = '';
    applySearch();
  }
});
content.addEventListener('scroll', updateCurrent, { passive: true });
window.addEventListener('hashchange', showHash);
// Shortcuts may have changed in Settings meanwhile (004: live).
window.addEventListener('focus', () => void refreshGenerated());

buildToc();
void refreshGenerated().then(showHash);
