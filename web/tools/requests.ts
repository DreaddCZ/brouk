/**
 * Requests tool (006): the tab's requests since its dev tools were turned
 * on, with filters (type, method, status, text; fetch/XHR by default),
 * details with headers and bodies, and export of selected requests as a
 * Postman Collection v2.1 (Postman and Bruno import it).
 */
import { button, element, errorText } from '../shared/dom.js';
import type { ToolsApi } from './api.js';
import {
  exportFileNames, matchesFilters, parseSecretHeaders, toPostman, type Filters, type RequestDetails,
  type RequestSummary, type StatusFilter, type TypeFilter,
} from './requests_logic.js';
import { formatBytes } from './site_data_logic.js';

export class RequestsTool {
  readonly #api: ToolsApi;
  readonly #root: HTMLElement;
  #version = -1;
  #requests: RequestSummary[] = [];
  #filters: Filters = { type: 'fetch', method: '', status: 'all', text: '' };
  #selected = new Set<number>();
  #open: RequestDetails | null = null;
  #body: string | null = null;
  /** The response body when it is JSON, for the JSON viewer's tree. */
  #bodyJson: { data: unknown; raw: string } | null = null;
  #error: string | null = null;
  #notice: string | null = null;
  #withEnvironment = true;
  #timer = 0;
  #list: HTMLElement | null = null;
  #details: HTMLElement | null = null;
  #count: HTMLElement | null = null;
  #exportButton: HTMLButtonElement | null = null;

  constructor(api: ToolsApi, root: HTMLElement) {
    this.#api = api;
    this.#root = root;
  }

  start(): void {
    this.#build();
    void this.#poll();
    window.clearInterval(this.#timer);
    this.#timer = window.setInterval(() => void this.#poll(), 1000);
  }

  stop(): void {
    window.clearInterval(this.#timer);
    this.#timer = 0;
  }

  async #poll(): Promise<void> {
    try {
      const answer = await this.#api.requests(this.#version);
      if (answer.requests !== null) {
        this.#version = answer.version;
        this.#requests = answer.requests;
        this.#renderList();
      }
    } catch (error) {
      this.#show(errorText(error), null);
    }
  }

  #show(error: string | null, notice: string | null): void {
    this.#error = error;
    this.#notice = notice;
    this.#renderDetails();
  }

  #build(): void {
    const bar = element('div', { className: 'toolbar' });
    const text = element('input');
    text.type = 'search';
    text.placeholder = 'Filter by URL or status';
    text.setAttribute('aria-label', 'Filter by URL or status');
    text.value = this.#filters.text;
    text.addEventListener('input', () => { this.#filters = { ...this.#filters, text: text.value }; this.#renderList(); });
    const select = (label: string, options: [string, string][], value: string, change: (v: string) => void): HTMLSelectElement => {
      const node = element('select');
      node.setAttribute('aria-label', label);
      for (const [v, title] of options) {
        const option = element('option', { text: title });
        option.value = v;
        option.selected = v === value;
        node.append(option);
      }
      node.addEventListener('change', () => { change(node.value); this.#renderList(); });
      return node;
    };
    const type = select('Type', [['fetch', 'Fetch/XHR'], ['all', 'All types'], ['document', 'Document'],
      ['script', 'Script'], ['stylesheet', 'CSS'], ['image', 'Image'], ['other', 'Other']], this.#filters.type,
      (v) => { this.#filters = { ...this.#filters, type: v as TypeFilter }; });
    const method = select('Method', [['', 'All methods'], ['GET', 'GET'], ['POST', 'POST'], ['PUT', 'PUT'],
      ['PATCH', 'PATCH'], ['DELETE', 'DELETE'], ['OPTIONS', 'OPTIONS']], this.#filters.method,
      (v) => { this.#filters = { ...this.#filters, method: v }; });
    const status = select('Status', [['all', 'All statuses'], ['2xx', '2xx'], ['3xx', '3xx'], ['4xx', '4xx'],
      ['5xx', '5xx'], ['failed', 'Failed']], this.#filters.status,
      (v) => { this.#filters = { ...this.#filters, status: v as StatusFilter }; });
    this.#count = element('span', { className: 'note' });
    const clear = button('Clear', () => void this.#api.clearRequests().then(() => {
      this.#selected.clear();
      this.#open = null;
      this.#renderDetails();
      return this.#poll();
    }));
    const environment = element('label', { className: 'note' });
    const box = element('input');
    box.type = 'checkbox';
    box.checked = this.#withEnvironment;
    box.addEventListener('change', () => { this.#withEnvironment = box.checked; });
    environment.append(box, ' with environment file (secrets)');
    this.#exportButton = button('Export to Postman…', () => void this.#export(), 'primary');
    bar.append(text, type, method, status, this.#count, clear, this.#exportButton, environment);

    this.#list = element('div', { className: 'request-list' });
    this.#details = element('div', { className: 'request-details' });
    this.#root.replaceChildren(bar, this.#list, this.#details);
    this.#renderList();
    this.#renderDetails();
  }

  #renderList(): void {
    if (this.#list === null) return;
    const shown = this.#requests.filter((r) => matchesFilters(r, this.#filters));
    const table = element('table');
    const head = element('tr');
    const all = element('input');
    all.type = 'checkbox';
    all.setAttribute('aria-label', 'Select all shown');
    all.checked = shown.length > 0 && shown.every((r) => this.#selected.has(r.seq));
    all.addEventListener('change', () => {
      for (const r of shown) {
        if (all.checked) this.#selected.add(r.seq); else this.#selected.delete(r.seq);
      }
      this.#renderList();
    });
    const allCell = element('th', { className: 'check' });
    allCell.append(all);
    head.append(allCell);
    for (const title of ['Method', 'Status', 'URL', 'Type', 'Size', 'Time']) head.append(element('th', { text: title }));
    table.append(head);
    // Newest at the bottom, as in Chrome.
    for (const request of shown.slice(-500)) {
      const row = element('tr', { className: this.#open?.seq === request.seq ? 'open' : '' });
      const box = element('input');
      box.type = 'checkbox';
      box.checked = this.#selected.has(request.seq);
      box.setAttribute('aria-label', `Select ${request.method} ${request.url}`);
      box.addEventListener('click', (event) => event.stopPropagation());
      box.addEventListener('change', () => {
        if (box.checked) this.#selected.add(request.seq); else this.#selected.delete(request.seq);
        this.#updateCount(shown.length);
      });
      const check = element('td', { className: 'check' });
      check.append(box);
      const status = request.error !== '' ? '(failed)' : request.status === 0 ? '…' : String(request.status);
      const cells = [request.method, status, request.url, request.type,
        request.size >= 0 ? formatBytes(request.size) : '', request.duration >= 0 ? `${Math.round(request.duration * 1000)} ms` : ''];
      row.append(check, ...cells.map((text) => {
        const cell = element('td', { text });
        cell.title = text;
        return cell;
      }));
      if (request.error !== '' || request.status >= 400) row.classList.add('failed');
      row.addEventListener('click', () => void this.#openRequest(request.seq));
      table.append(row);
    }
    this.#list.replaceChildren(table);
    if (shown.length === 0) {
      this.#list.append(element('div', { className: 'empty', text:
        this.#requests.length === 0 ? 'No requests yet. They are recorded while the developer tools are on.'
          : 'No requests match the filters.' }));
    }
    this.#updateCount(shown.length);
  }

  #updateCount(shown: number): void {
    if (this.#count) this.#count.textContent = `${shown} of ${this.#requests.length}, ${this.#selected.size} selected`;
    if (this.#exportButton) this.#exportButton.disabled = this.#selected.size === 0;
  }

  async #openRequest(seq: number): Promise<void> {
    try {
      this.#open = await this.#api.request(seq);
      this.#body = null;
      this.#bodyJson = null;
      this.#error = null;
    } catch (error) {
      this.#error = errorText(error);
    }
    this.#renderList();
    this.#renderDetails();
  }

  async #loadBody(): Promise<void> {
    if (this.#open === null) return;
    try {
      const result = await this.#api.protocol('Network.getResponseBody', { requestId: this.#open.id });
      const body = typeof result['body'] === 'string' ? result['body'] : '';
      const text = result['base64Encoded'] === true ? `(binary, ${formatBytes(Math.floor(body.length * 3 / 4))})` : body;
      this.#body = prettyJson(text);
      this.#bodyJson = null;
      if (result['base64Encoded'] !== true) {
        try {
          this.#bodyJson = { data: JSON.parse(body) as unknown, raw: body };
        } catch { /* not JSON: shown as text */ }
      }
    } catch (error) {
      this.#body = `The body is not available: ${errorText(error)}`;
    }
    this.#renderDetails();
  }

  #renderDetails(): void {
    if (this.#details === null) return;
    const parts: HTMLElement[] = [];
    if (this.#error !== null) parts.push(element('p', { className: 'error', text: this.#error }));
    if (this.#notice !== null) parts.push(element('p', { className: 'note', text: this.#notice }));
    const open = this.#open;
    if (open !== null) {
      parts.push(element('h2', { text: `${open.method} ${open.url}` }));
      const general = element('p', { className: 'note', text:
        `${open.error !== '' ? open.error : `Status ${open.status}`} · ${open.type} · ${open.mimeType}` });
      parts.push(general);
      parts.push(headersTable('Request headers', open.requestHeaders));
      if (open.postData !== undefined) {
        parts.push(element('h2', { text: open.postDataTruncated ? 'Request body (shortened)' : 'Request body' }));
        parts.push(element('pre', { className: 'body', text: prettyJson(open.postData) }));
      }
      parts.push(headersTable('Response headers', open.responseHeaders));
      const bodyHeading = element('h2', { text: 'Response body' });
      if (this.#body === null) bodyHeading.append(button('Load', () => void this.#loadBody(), 'small'));
      parts.push(bodyHeading);
      if (this.#bodyJson !== null && typeof jsonViewRender === 'function') {
        const tree = element('div', { className: 'json-body' });
        jsonViewRender(tree, this.#bodyJson.data, this.#bodyJson.raw);
        parts.push(tree);
      } else if (this.#body !== null) {
        parts.push(element('pre', { className: 'body', text: this.#body }));
      }
    }
    this.#details.replaceChildren(...parts);
  }

  async #export(): Promise<void> {
    try {
      const page = await this.#api.page();
      const selected = this.#requests.filter((r) => this.#selected.has(r.seq));
      const details: RequestDetails[] = [];
      for (const request of selected) details.push(await this.#api.request(request.seq));
      let name = 'Requests';
      try {
        name = new URL(page.url).host || name;
      } catch { /* keep the default */ }
      const { collection, environment } = toPostman(details, name, parseSecretHeaders(page.secretHeaders));
      const files = exportFileNames(name);
      const saved = await this.#api.save(
        { name: files.collection, content: JSON.stringify(collection, null, 2) },
        this.#withEnvironment ? { name: files.environment, content: JSON.stringify(environment, null, 2) } : null,
      );
      if (saved) {
        this.#show(null, this.#withEnvironment
          ? `Exported ${details.length} requests. Import both files into Postman or Bruno; the environment file contains the secrets.`
          : `Exported ${details.length} requests. The collection contains no secret values.`);
      }
    } catch (error) {
      this.#show(errorText(error), null);
    }
  }
}

function headersTable(title: string, headers: readonly (readonly [string, string])[]): HTMLElement {
  const box = element('div');
  box.append(element('h2', { text: title }));
  const table = element('table', { className: 'headers' });
  for (const [name, value] of headers) {
    const row = element('tr');
    row.append(element('th', { text: name }), element('td', { text: value }));
    table.append(row);
  }
  if (headers.length === 0) box.append(element('div', { className: 'empty', text: 'None recorded.' }));
  box.append(table);
  return box;
}

/** Pretty JSON if the text is JSON, else the text. */
function prettyJson(text: string): string {
  const start = text.trimStart();
  if (!start.startsWith('{') && !start.startsWith('[')) return text;
  try {
    return JSON.stringify(JSON.parse(text), null, 2);
  } catch {
    return text;
  }
}
