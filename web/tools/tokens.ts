/**
 * Tokens tool (006): JWTs from the tab's request and response headers,
 * cookies and storage, and pasted tokens, decoded. Signatures are not
 * verified (shown), and tokens never leave the browser.
 */
import { button, element, errorText } from '../shared/dom.js';
import type { ToolsApi } from './api.js';
import { parseCookies, parseStorageItems, siteOrigin } from './site_data_logic.js';
import { decodeJwt, findJwts, isExpired, summarizeClaims } from './tokens_logic.js';

interface Found {
  readonly token: string;
  readonly where: string;
}

export class TokensTool {
  readonly #api: ToolsApi;
  readonly #root: HTMLElement;
  #found: Found[] = [];
  #pasted = '';
  #error: string | null = null;

  constructor(api: ToolsApi, root: HTMLElement) {
    this.#api = api;
    this.#root = root;
  }

  get busy(): boolean {
    return document.activeElement instanceof HTMLTextAreaElement;
  }

  async refresh(): Promise<void> {
    const found: Found[] = [];
    const seen = new Set<string>();
    const add = (token: string, where: string): void => {
      if (!seen.has(token)) {
        seen.add(token);
        found.push({ token, where });
      }
    };
    try {
      for (const token of await this.#api.tokens()) add(token.token, token.where);
      const url = await this.#api.pageUrl();
      const origin = siteOrigin(url);
      if (origin !== null) {
        for (const cookie of parseCookies(await this.#api.protocol('Network.getCookies', { urls: [url] }))) {
          for (const token of findJwts(cookie.value)) add(token, `Cookie ${cookie.name}`);
        }
        for (const local of [true, false]) {
          const items = parseStorageItems(await this.#api.protocol('DOMStorage.getDOMStorageItems', {
            storageId: { securityOrigin: origin, isLocalStorage: local } }));
          for (const [key, value] of items) {
            for (const token of findJwts(value)) add(token, `${local ? 'Local' : 'Session'} storage ${key}`);
          }
        }
      }
      this.#error = null;
    } catch (error) {
      this.#error = errorText(error);
    }
    this.#found = found;
    this.render();
  }

  render(): void {
    const parts: HTMLElement[] = [];
    const bar = element('div', { className: 'toolbar' });
    bar.append(element('span', { className: 'origin', text: 'Tokens' }),
               element('span', { className: 'note', text: 'Signatures are not verified. Tokens stay in the browser.' }),
               button('Refresh', () => void this.refresh()));
    parts.push(bar);
    if (this.#error !== null) parts.push(element('p', { className: 'error', text: this.#error }));

    const paste = element('textarea', { className: 'paste' });
    paste.placeholder = 'Paste a token to decode it';
    paste.setAttribute('aria-label', 'Paste a token to decode it');
    paste.value = this.#pasted;
    const pastedResult = element('div');
    const showPasted = (): void => {
      this.#pasted = paste.value;
      const value = paste.value.trim().replace(/^Bearer\s+/i, '');
      pastedResult.replaceChildren(...(value === '' ? [] : [tokenCard({ token: value, where: 'Pasted' })]));
    };
    paste.addEventListener('input', showPasted);
    showPasted();
    parts.push(paste, pastedResult);

    parts.push(element('h2', { text: `Found in this tab (${this.#found.length})` }));
    if (this.#found.length === 0) {
      parts.push(element('div', { className: 'empty', text:
        'No tokens yet. They are found in requests recorded while the developer tools are on, and in the ' +
        "site's cookies and storage." }));
    }
    parts.push(...this.#found.map(tokenCard));
    this.#root.replaceChildren(...parts);
  }
}

function tokenCard(found: Found): HTMLElement {
  const card = element('section', { className: 'token' });
  const decoded = decodeJwt(found.token);
  const now = Date.now() / 1000;
  const title = element('div', { className: 'token-title' });
  title.append(element('span', { className: 'where', text: found.where }));
  if (decoded === null) {
    title.append(element('span', { className: 'badge muted', text: 'Not decodable (not a JWT)' }));
  } else if (isExpired(decoded.payload, now)) {
    title.append(element('span', { className: 'badge expired', text: 'Expired' }));
  } else {
    title.append(element('span', { className: 'badge', text: 'Signature not verified' }));
  }
  card.append(title, element('div', { className: 'token-text', text: found.token }));
  if (decoded !== null) {
    const claims = element('table', { className: 'claims' });
    for (const claim of summarizeClaims(decoded.payload, now)) {
      const row = element('tr');
      row.append(element('th', { text: claim.label }), element('td', { text: claim.value }));
      claims.append(row);
    }
    const raw = element('details');
    raw.append(element('summary', { text: 'Header and payload' }),
               element('pre', { className: 'body', text:
                 `${JSON.stringify(decoded.header, null, 2)}\n${JSON.stringify(decoded.payload, null, 2)}` }));
    card.append(claims, raw);
  }
  return card;
}
