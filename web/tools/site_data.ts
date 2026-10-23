/**
 * Site data tool (006): cookies and local/session storage of the current
 * site in the tab's profile (view, edit, add, delete), IndexedDB
 * databases, cache storage and service workers (delete), and "Clear site
 * data". Works only through the dock's own tab (and so its profile).
 */
import { button, element, errorText } from '../shared/dom.js';
import type { ToolsApi } from './api.js';
import {
  describeServiceWorker, formatBytes, formatExpiry, parseCookies, parseServiceWorkers, parseStorageItems,
  setCookieParams, siteOrigin, type Cookie, type ServiceWorker,
} from './site_data_logic.js';

interface State {
  url: string;
  origin: string | null;
  cookies: Cookie[];
  local: [string, string][];
  session: [string, string][];
  databases: string[];
  caches: { id: string; name: string }[];
  serviceWorkerBytes: number;
  serviceWorkers: ServiceWorker[];
}

export class SiteDataTool {
  readonly #api: ToolsApi;
  readonly #root: HTMLElement;
  #state: State | null = null;
  #error: string | null = null;
  #confirmClear = false;
  /** Row being edited: "cookie:<index>", "local:<key>", "new-cookie", ... */
  #editing: string | null = null;

  constructor(api: ToolsApi, root: HTMLElement) {
    this.#api = api;
    this.#root = root;
  }

  /** Something is being edited or confirmed: do not refresh under it. */
  get busy(): boolean {
    return this.#editing !== null || this.#confirmClear;
  }

  async refresh(): Promise<void> {
    try {
      const url = await this.#api.pageUrl();
      const origin = siteOrigin(url);
      const state: State = {
        url, origin, cookies: [], local: [], session: [], databases: [], caches: [], serviceWorkerBytes: 0,
        serviceWorkers: [],
      };
      if (origin !== null) {
        state.cookies = parseCookies(await this.#api.protocol('Network.getCookies', { urls: [url] }));
        state.local = parseStorageItems(await this.#api.protocol('DOMStorage.getDOMStorageItems', {
          storageId: { securityOrigin: origin, isLocalStorage: true } }));
        state.session = parseStorageItems(await this.#api.protocol('DOMStorage.getDOMStorageItems', {
          storageId: { securityOrigin: origin, isLocalStorage: false } }));
        const dbs = await this.#api.protocol('IndexedDB.requestDatabaseNames', { securityOrigin: origin })
          .catch(() => ({}));
        const names: unknown = (dbs as Record<string, unknown>)['databaseNames'];
        state.databases = Array.isArray(names) ? names.filter((n): n is string => typeof n === 'string') : [];
        const caches = await this.#api.protocol('CacheStorage.requestCacheNames', { securityOrigin: origin })
          .catch(() => ({}));
        const list: unknown = (caches as Record<string, unknown>)['caches'];
        state.caches = Array.isArray(list)
          ? list.flatMap((c: unknown) => {
              const item = c as Record<string, unknown>;
              return typeof item['cacheId'] === 'string' && typeof item['cacheName'] === 'string'
                ? [{ id: item['cacheId'], name: item['cacheName'] }] : [];
            })
          : [];
        const usage = await this.#api.protocol('Storage.getUsageAndQuota', { origin }).catch(() => ({}));
        const breakdown: unknown = (usage as Record<string, unknown>)['usageBreakdown'];
        state.serviceWorkerBytes = Array.isArray(breakdown)
          ? breakdown.reduce((sum: number, entry: unknown) => {
              const item = entry as Record<string, unknown>;
              return item['storageType'] === 'service_workers' && typeof item['usage'] === 'number'
                ? sum + item['usage'] : sum;
            }, 0)
          : 0;
        state.serviceWorkers = parseServiceWorkers(await this.#api.serviceWorkers().catch(() => null), origin);
      }
      this.#state = state;
      this.#error = null;
    } catch (error) {
      this.#error = errorText(error);
    }
    this.render();
  }

  /** Runs a change, shows its error if any, and reloads the data. */
  async #change(action: () => Promise<unknown>): Promise<void> {
    try {
      await action();
      this.#editing = null;
      this.#error = null;
    } catch (error) {
      this.#error = errorText(error);
    }
    await this.refresh();
  }

  render(): void {
    const state = this.#state;
    const parts: HTMLElement[] = [];
    const bar = element('div', { className: 'toolbar' });
    bar.append(element('span', { className: 'origin', text: state?.origin ?? 'This page has no site data.' }));
    bar.append(button('Refresh', () => void this.refresh()));
    if (state?.origin) {
      bar.append(button('Clear site data…', () => { this.#confirmClear = true; this.render(); }, 'danger'));
    }
    parts.push(bar);
    if (this.#error !== null) parts.push(element('p', { className: 'error', text: this.#error }));
    if (state?.origin && this.#confirmClear) parts.push(this.#confirmBox(state.origin));
    if (state?.origin) {
      parts.push(...this.#cookies(state), ...this.#storage(state, true), ...this.#storage(state, false),
                 ...this.#others(state));
    }
    this.#root.replaceChildren(...parts);
  }

  #confirmBox(origin: string): HTMLElement {
    const box = element('div', { className: 'confirm' });
    box.setAttribute('role', 'alert');
    box.append(element('p', { text: `Delete all data of ${origin} in this profile: cookies, storage, ` +
      'databases, caches and service workers? Other sites and other profiles keep theirs. You will be ' +
      'signed out of this site.' }));
    box.append(
      button('Delete', () => void this.#clearAll(origin), 'danger'),
      ' ',
      button('Cancel', () => { this.#confirmClear = false; this.render(); }),
    );
    return box;
  }

  async #clearAll(origin: string): Promise<void> {
    this.#confirmClear = false;
    const url = this.#state?.url ?? origin;
    await this.#change(async () => {
      await this.#api.protocol('Storage.clearDataForOrigin', { origin, storageTypes: 'all' });
      // Cookies of the page's domain that the origin does not cover (set
      // for a parent domain), and session storage.
      const cookies = parseCookies(await this.#api.protocol('Network.getCookies', { urls: [url] }));
      for (const cookie of cookies) {
        await this.#api.protocol('Network.deleteCookies', { name: cookie.name, domain: cookie.domain, path: cookie.path });
      }
      await this.#api.protocol('DOMStorage.clear', { storageId: { securityOrigin: origin, isLocalStorage: false } });
    });
  }

  #heading(title: string, count: number, add?: () => void): HTMLElement {
    const heading = element('h2', { text: title });
    heading.append(element('span', { className: 'count', text: String(count) }));
    if (add) heading.append(button('Add', add, 'small'));
    return heading;
  }

  #cookies(state: State): HTMLElement[] {
    const origin = state.origin ?? '';
    const table = element('table');
    const head = element('tr');
    for (const title of ['Name', 'Value', 'Domain', 'Path', 'Expires', 'Flags', '']) {
      head.append(element('th', { text: title }));
    }
    table.append(head);
    const blank: Cookie = { name: '', value: '', domain: '', path: '/', expires: -1, httpOnly: false,
      secure: origin.startsWith('https:'), sameSite: 'Lax' };
    const rows: [string, Cookie][] = state.cookies.map((cookie, index) => [`cookie:${index}`, cookie]);
    if (this.#editing === 'new-cookie') rows.push(['new-cookie', blank]);
    for (const [id, cookie] of rows) {
      const row = element('tr');
      if (this.#editing === id) {
        const name = this.#input(cookie.name, 'Name');
        const value = this.#input(cookie.value, 'Value');
        const domain = this.#input(cookie.domain, 'Domain (empty: this host)');
        const path = this.#input(cookie.path, 'Path');
        row.append(this.#cell(name), this.#cell(value), this.#cell(domain), this.#cell(path),
                   element('td', { text: formatExpiry(cookie.expires) }), element('td', { className: 'flags' }));
        const actions = element('td', { className: 'actions' });
        actions.append(button('Save', () => void this.#change(async () => {
          const edited = { ...cookie, name: name.value, value: value.value, domain: domain.value, path: path.value };
          // Renaming or moving a cookie creates a new one: remove the old.
          if (id !== 'new-cookie' && (edited.name !== cookie.name || edited.domain !== cookie.domain ||
                                     edited.path !== cookie.path)) {
            await this.#api.protocol('Network.deleteCookies', { name: cookie.name, domain: cookie.domain, path: cookie.path });
          }
          const result = await this.#api.protocol('Network.setCookie', setCookieParams(edited, origin));
          if (result['success'] === false) throw new Error('The cookie was not accepted (check its domain and flags).');
        }), 'small primary'), ' ', button('Cancel', () => { this.#editing = null; this.render(); }, 'small'));
        row.append(actions);
      } else {
        const flags = [cookie.httpOnly ? 'HttpOnly' : '', cookie.secure ? 'Secure' : '', cookie.sameSite]
          .filter((f) => f !== '').join(' ');
        for (const text of [cookie.name, cookie.value, cookie.domain, cookie.path, formatExpiry(cookie.expires)]) {
          const cell = element('td', { text });
          cell.title = text;
          row.append(cell);
        }
        row.append(element('td', { className: 'flags', text: flags }));
        const actions = element('td', { className: 'actions' });
        actions.append(
          button('Edit', () => { this.#editing = id; this.render(); }, 'small'), ' ',
          button('Delete', () => void this.#change(() => this.#api.protocol('Network.deleteCookies', {
            name: cookie.name, domain: cookie.domain, path: cookie.path })), 'small danger'),
        );
        row.append(actions);
      }
      table.append(row);
    }
    const parts = [this.#heading('Cookies', state.cookies.length, () => { this.#editing = 'new-cookie'; this.render(); }), table];
    if (rows.length === 0) parts.push(element('div', { className: 'empty', text: 'No cookies.' }));
    return parts;
  }

  #storage(state: State, local: boolean): HTMLElement[] {
    const kind = local ? 'local' : 'session';
    const entries = local ? state.local : state.session;
    const storageId = { securityOrigin: state.origin, isLocalStorage: local };
    const table = element('table');
    const head = element('tr');
    for (const title of ['Key', 'Value', '']) head.append(element('th', { text: title }));
    table.append(head);
    const rows: [string, string, string][] = entries.map(([key, value]) => [`${kind}:${key}`, key, value]);
    if (this.#editing === `new-${kind}`) rows.push([`new-${kind}`, '', '']);
    for (const [id, key, value] of rows) {
      const row = element('tr');
      if (this.#editing === id) {
        const keyInput = this.#input(key, 'Key');
        const valueInput = this.#input(value, 'Value');
        row.append(this.#cell(keyInput), this.#cell(valueInput));
        const actions = element('td', { className: 'actions' });
        actions.append(button('Save', () => void this.#change(async () => {
          if (keyInput.value === '') throw new Error('Enter a key.');
          if (!id.startsWith('new-') && keyInput.value !== key) {
            await this.#api.protocol('DOMStorage.removeDOMStorageItem', { storageId, key });
          }
          await this.#api.protocol('DOMStorage.setDOMStorageItem', { storageId, key: keyInput.value, value: valueInput.value });
        }), 'small primary'), ' ', button('Cancel', () => { this.#editing = null; this.render(); }, 'small'));
        row.append(actions);
      } else {
        for (const text of [key, value]) {
          const cell = element('td', { text });
          cell.title = text;
          row.append(cell);
        }
        const actions = element('td', { className: 'actions' });
        actions.append(
          button('Edit', () => { this.#editing = id; this.render(); }, 'small'), ' ',
          button('Delete', () => void this.#change(() =>
            this.#api.protocol('DOMStorage.removeDOMStorageItem', { storageId, key })), 'small danger'),
        );
        row.append(actions);
      }
      table.append(row);
    }
    const parts = [this.#heading(local ? 'Local storage' : 'Session storage', entries.length,
                                 () => { this.#editing = `new-${kind}`; this.render(); }), table];
    if (rows.length === 0) parts.push(element('div', { className: 'empty', text: 'Empty.' }));
    return parts;
  }

  #others(state: State): HTMLElement[] {
    const origin = state.origin ?? '';
    const list = (items: [string, () => Promise<unknown>][], empty: string): HTMLElement => {
      const table = element('table');
      for (const [name, remove] of items) {
        const row = element('tr');
        row.append(element('td', { text: name }));
        const actions = element('td', { className: 'actions' });
        actions.append(button('Delete', () => void this.#change(remove), 'small danger'));
        row.append(actions);
        table.append(row);
      }
      if (items.length === 0) table.append(element('tr', { className: 'empty', text: empty }));
      return table;
    };
    // Each registration by its scope; if none is known but the site
    // stores service worker data, that data can still be deleted.
    const workers: [string, () => Promise<unknown>][] = state.serviceWorkers.length > 0
      ? state.serviceWorkers.map((worker) => {
          const about = describeServiceWorker(worker);
          return [about === '' ? worker.scope : `${worker.scope} (${about})`, () =>
            this.#api.protocol('ServiceWorker.unregister', { scopeURL: worker.scope })];
        })
      : state.serviceWorkerBytes > 0
        ? [[`Registered (${formatBytes(state.serviceWorkerBytes)})`, () =>
            this.#api.protocol('Storage.clearDataForOrigin', { origin, storageTypes: 'service_workers' })]]
        : [];
    return [
      this.#heading('IndexedDB databases', state.databases.length),
      list(state.databases.map((name) => [name, () =>
        this.#api.protocol('IndexedDB.deleteDatabase', { securityOrigin: origin, databaseName: name })]), 'None.'),
      this.#heading('Cache storage', state.caches.length),
      list(state.caches.map((cache) => [cache.name, () =>
        this.#api.protocol('CacheStorage.deleteCache', { cacheId: cache.id })]), 'None.'),
      this.#heading('Service workers', workers.length),
      list(workers, 'None.'),
    ];
  }

  #input(value: string, label: string): HTMLInputElement {
    const input = element('input');
    input.type = 'text';
    input.value = value;
    input.placeholder = label;
    input.setAttribute('aria-label', label);
    input.spellcheck = false;
    return input;
  }

  #cell(content: HTMLElement): HTMLTableCellElement {
    const cell = element('td');
    cell.append(content);
    return cell;
  }
}
