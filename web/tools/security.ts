/**
 * Security check tool (029): passive checks of the dock's tab — its
 * headers, cookies, storage and elements — listed by severity, with
 * Copy as Markdown and findings hidden per site. Sends no request of its
 * own; it reads what the tab already loaded.
 */
import { button, element, errorText } from '../shared/dom.js';
import type { SecurityAnswer, ToolsApi } from './api.js';
import {
  findingsMarkdown, kSeverities, markdownFileName, runSecurityChecks, severityName, summary, type CheckResult, type Finding,
  type PageElements, type SecurityInput,
} from './security_logic.js';
import { parseCookies, parseStorageItems, siteOrigin } from './site_data_logic.js';
import { copyText } from './utilities_ui.js';

/** At most this many elements of a kind are read. */
const kMaxElements = 300;

interface Checked {
  readonly answer: SecurityAnswer;
  readonly result: CheckResult;
  readonly at: Date;
}

export class SecurityTool {
  readonly #api: ToolsApi;
  readonly #root: HTMLElement;
  #checked: Checked | null = null;
  #hidden = new Set<string>();
  #showHidden = false;
  #error: string | null = null;
  #status: string | null = null;
  #running = false;

  constructor(api: ToolsApi, root: HTMLElement) {
    this.#api = api;
    this.#root = root;
  }

  get busy(): boolean {
    return this.#running;
  }

  async refresh(): Promise<void> {
    if (this.#running) return;
    this.#running = true;
    try {
      const answer = await this.#api.securityCheck();
      this.#hidden = new Set(answer.hidden);
      if (siteOrigin(answer.url) === null) {
        this.#checked = null;
        this.#error = 'The security check works on web pages (http and https).';
      } else {
        const input = await this.#gather(answer);
        this.#checked = { answer, result: runSecurityChecks(input), at: new Date() };
        this.#error = null;
      }
    } catch (error) {
      this.#error = errorText(error);
    } finally {
      this.#running = false;
    }
    this.render();
  }

  /** Reloads the tab and checks once its load has been recorded. */
  async #reloadAndCheck(): Promise<void> {
    this.#status = 'Reloading…';
    this.render();
    try {
      await this.#api.reloadTab();
      // Wait for the new load (up to 15 s), then a moment for its requests.
      for (let i = 0; i < 30; i += 1) {
        await new Promise((resolve) => setTimeout(resolve, 500));
        const answer = await this.#api.securityCheck();
        if (answer.document !== null) break;
      }
      await new Promise((resolve) => setTimeout(resolve, 1000));
    } catch (error) {
      this.#error = errorText(error);
    }
    this.#status = null;
    await this.refresh();
  }

  async #gather(answer: SecurityAnswer): Promise<SecurityInput> {
    const origin = siteOrigin(answer.url) ?? '';
    const cookies = parseCookies(await this.#api.protocol('Network.getCookies', { urls: [answer.url] }).catch(() => ({})));
    const storage: SecurityInput['storage'][number][] = [];
    for (const local of [true, false]) {
      const items = parseStorageItems(await this.#api.protocol('DOMStorage.getDOMStorageItems', {
        storageId: { securityOrigin: origin, isLocalStorage: local } }).catch(() => ({})));
      for (const [key, value] of items) storage.push({ area: local ? 'local' : 'session', key, value });
    }
    return {
      url: answer.url,
      site: answer.site,
      document: answer.document,
      siteRequests: answer.siteRequests,
      pageRequests: answer.pageRequests,
      cookies,
      storage,
      elements: await this.#elements(answer.url).catch(() => null),
      sessionCookies: answer.sessionCookies,
      secretParameters: answer.secretParameters,
      now: Date.now() / 1000,
    };
  }

  /** Reads the page's forms, password fields, scripts and loads through the protocol (read only). */
  async #elements(pageUrl: string): Promise<PageElements> {
    const documentAnswer = await this.#api.protocol('DOM.getDocument', { depth: 0 });
    const root = documentAnswer['root'];
    const rootId = typeof root === 'object' && root !== null ? (root as Record<string, unknown>)['nodeId'] : undefined;
    if (typeof rootId !== 'number') throw new Error('No document.');
    const select = async (selector: string): Promise<Map<string, string>[]> => {
      const found = await this.#api.protocol('DOM.querySelectorAll', { nodeId: rootId, selector });
      const ids: unknown = found['nodeIds'];
      if (!Array.isArray(ids)) return [];
      const nodes = ids.filter((id): id is number => typeof id === 'number').slice(0, kMaxElements);
      return Promise.all(nodes.map(async (nodeId) => {
        const attributes: unknown = (await this.#api.protocol('DOM.getAttributes', { nodeId }))['attributes'];
        const map = new Map<string, string>();
        if (Array.isArray(attributes)) {
          for (let i = 0; i + 1 < attributes.length; i += 2) {
            const [name, value]: unknown[] = [attributes[i], attributes[i + 1]];
            if (typeof name === 'string' && typeof value === 'string') map.set(name.toLowerCase(), value);
          }
        }
        return map;
      }));
    };
    const forms = await select('form');
    const passwords = await select('input[type=password i]');
    const scripts = await select('script[src]');
    const styles = await select('link[rel~=stylesheet i][href]');
    const loads = await select('img[src^="http:" i], script[src^="http:" i], iframe[src^="http:" i], '
      + 'video[src^="http:" i], audio[src^="http:" i], source[src^="http:" i], link[href^="http:" i]');
    const metas = await select('meta[http-equiv="content-security-policy" i][content]');
    const absolute = (url: string): string => {
      try {
        return new URL(url, pageUrl).href;
      } catch {
        return url;
      }
    };
    return {
      formActions: forms.map((f) => f.get('action') ?? ''),
      passwordFields: passwords.length,
      subresources: [
        ...scripts.map((s) => ({ url: absolute(s.get('src') ?? ''), integrity: s.get('integrity') ?? '' })),
        ...styles.map((s) => ({ url: absolute(s.get('href') ?? ''), integrity: s.get('integrity') ?? '' })),
      ],
      loads: loads.map((l) => l.get('src') ?? l.get('href') ?? ''),
      metaPolicies: metas.map((m) => m.get('content') ?? ''),
    };
  }

  /** Saves the findings as a .md file where the user chooses. */
  async #download(checked: Checked): Promise<void> {
    try {
      await this.#api.save({
        name: markdownFileName(checked.answer.url, checked.at),
        content: findingsMarkdown(checked.result, checked.answer.url, this.#hidden, checked.at),
      }, null);
      this.#error = null;
    } catch (error) {
      this.#error = errorText(error);
      this.render();
    }
  }

  async #setHidden(id: string, hide: boolean): Promise<void> {
    const next = new Set(this.#hidden);
    if (hide) next.add(id);
    else next.delete(id);
    try {
      await this.#api.setSecurityHidden([...next]);
      this.#hidden = next;
      this.#error = null;
    } catch (error) {
      this.#error = errorText(error);
    }
    this.render();
  }

  render(): void {
    const parts: HTMLElement[] = [];
    const checked = this.#checked;
    const bar = element('div', { className: 'toolbar' });
    bar.append(element('span', { className: 'origin', text: checked ? `Security check: ${checked.answer.url}` : 'Security check' }));
    const again = button(this.#running ? 'Checking…' : 'Check again', () => void this.refresh());
    again.disabled = this.#running || this.#status !== null;
    bar.append(again);
    if (checked !== null) {
      const copy = button('Copy as Markdown', () => {
        void copyText(findingsMarkdown(checked.result, checked.answer.url, this.#hidden, checked.at)).then(() => {
          copy.textContent = 'Copied';
          setTimeout(() => { copy.textContent = 'Copy as Markdown'; }, 1500);
        });
      });
      bar.append(copy, button('Download as Markdown', () => void this.#download(checked)));
    }
    parts.push(bar);
    parts.push(element('p', { className: 'note security-intro',
      text: 'Passive checks of what this tab loaded: no request is sent. Ratings as in OWASP ZAP. A clean result is not a security test.' }));
    if (this.#status !== null) parts.push(element('p', { className: 'empty', text: this.#status }));
    if (this.#error !== null) parts.push(element('p', { className: 'error', text: this.#error }));
    if (checked === null) {
      this.#root.replaceChildren(...parts);
      return;
    }
    const { result, answer } = checked;
    if (answer.document === null) {
      const notice = element('div', { className: 'security-reload' });
      notice.append(element('span', { text: 'The page loaded before the dev tools were on, so its headers are not known.' }),
        button('Reload and check', () => void this.#reloadAndCheck(), 'primary'));
      parts.push(notice);
    }
    const counts = element('p', { className: 'security-summary', text: summary(result, this.#hidden) });
    counts.setAttribute('role', 'status');
    parts.push(counts);
    for (const note of result.notes.filter((n) => answer.document !== null || !n.includes('Reload and check'))) {
      parts.push(element('p', { className: 'note', text: note }));
    }
    const shown = result.findings.filter((f) => this.#showHidden || !this.#hidden.has(f.id));
    if (shown.length === 0 && result.findings.length === 0) {
      parts.push(element('p', { className: 'empty', text: 'Nothing found.' }));
    }
    for (const severity of kSeverities) {
      const group = shown.filter((f) => f.severity === severity);
      if (group.length === 0) continue;
      const heading = element('h2', { text: severityName(severity) });
      heading.append(element('span', { className: 'count', text: String(group.length) }));
      parts.push(heading, ...group.map((f) => this.#findingView(f, answer.canHide)));
    }
    const hiddenCount = result.findings.filter((f) => this.#hidden.has(f.id)).length;
    if (hiddenCount > 0) {
      parts.push(button(this.#showHidden ? 'Leave out hidden findings' : `Show hidden (${hiddenCount})`, () => {
        this.#showHidden = !this.#showHidden;
        this.render();
      }, 'small'));
    }
    if (result.passed.length > 0) {
      const passed = element('details', { className: 'security-passed' });
      passed.append(element('summary', { text: `${result.passed.length} checks passed` }));
      const list = element('ul');
      list.append(...result.passed.map((title) => element('li', { text: title })));
      passed.append(list);
      parts.push(passed);
    }
    this.#root.replaceChildren(...parts);
  }

  #findingView(f: Finding, canHide: boolean): HTMLElement {
    const hidden = this.#hidden.has(f.id);
    const box = element('details', { className: `finding ${f.severity}${hidden ? ' hidden-finding' : ''}` });
    const head = element('summary');
    head.append(element('span', { className: `badge severity ${f.severity}`, text: severityName(f.severity) }),
      element('span', { className: 'finding-title', text: f.title }));
    if (hidden) head.append(element('span', { className: 'badge muted', text: 'Hidden' }));
    box.append(head);
    const table = element('table', { className: 'claims finding-details' });
    const row = (label: string, value: string): void => {
      const tr = element('tr');
      tr.append(element('th', { text: label }), element('td', { text: value }));
      table.append(tr);
    };
    row('Found', f.found);
    row('Why it matters', f.why);
    row('How to fix', f.fix);
    row('Rating', f.rating);
    box.append(table);
    const actions = element('div', { className: 'toolbar' });
    const link = element('a', { text: 'OWASP reference' });
    link.href = f.link; // nosemgrep: ts-url-sink -- constant OWASP https links (kLinks); the browser opens only those
    link.target = '_blank';
    link.rel = 'noopener';
    actions.append(link);
    if (canHide) {
      actions.append(button(hidden ? 'Show again' : 'Hide for this site', () => void this.#setHidden(f.id, !hidden), 'small'));
    }
    box.append(actions);
    return box;
  }
}
