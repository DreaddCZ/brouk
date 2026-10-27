/**
 * "?" buttons that open help at a section (016). Static pages mark them as
 * <button class="help-button" data-help="<anchor>">; code makes them with
 * helpButton(). The anchors are checked at build time (web/help/build.mjs).
 */
import { cefTransport, send } from './bridge.js';
import { element } from './dom.js';

/** Opens the help window at `anchor`; does nothing outside Brouk. */
export function openHelp(anchor: string): void {
  send('help.open', { anchor }, cefTransport()).catch(() => {
    // Help is a convenience: a page keeps working without it.
  });
}

/** A small "?" button that opens help at `anchor`. */
export function helpButton(anchor: string, topic = ''): HTMLButtonElement {
  const node = element('button', { className: 'help-button', text: '?' });
  node.type = 'button';
  node.dataset['help'] = anchor;
  const label = topic === '' ? 'Help' : `Help: ${topic}`;
  node.title = label;
  node.setAttribute('aria-label', label);
  node.addEventListener('click', (event) => {
    event.preventDefault();
    event.stopPropagation();
    openHelp(anchor);
  });
  return node;
}

/** Makes the page's static "?" buttons (data-help) work. */
export function wireHelpButtons(root: ParentNode = document): void {
  for (const node of root.querySelectorAll<HTMLElement>('[data-help]')) {
    if (node.dataset['helpWired'] === '1') continue;
    node.dataset['helpWired'] = '1';
    const anchor = node.dataset['help'] ?? '';
    if (!node.title) node.title = 'Help';
    if (!node.getAttribute('aria-label')) node.setAttribute('aria-label', 'Help');
    node.addEventListener('click', (event) => {
      event.preventDefault();
      event.stopPropagation();
      openHelp(anchor);
    });
  }
}

/** The help anchor of a settings page section, e.g. "settings-ad-blocking". */
export function settingsSectionAnchor(section: string): string {
  return `settings-${slug(section)}`;
}

/** "Secure DNS" -> "secure-dns". The same rule as web/help/build.mjs. */
export function slug(text: string): string {
  return text
    .normalize('NFD')
    .replace(/[̀-ͯ]/g, '')
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '');
}
