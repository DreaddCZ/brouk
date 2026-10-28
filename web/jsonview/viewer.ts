/**
 * JSON viewer (006): when a tab shows a JSON document, Brouk adds this
 * script to it and replaces the plain text with a collapsible tree, with
 * search, a raw/pretty switch, and copying of values and paths.
 *
 * It is a plain script (no modules): the renderer runs it in the document
 * before it loads. The data comes from the web, so it is only ever set as
 * text (textContent), never as HTML. Large documents stay responsive:
 * nodes are built when they are expanded, long arrays and objects in
 * chunks.
 *
 * The Requests tool (brouk://tools) loads the same file as
 * shared/json_viewer.js and shows response bodies with `jsonViewRender`.
 *
 * The functions before `jsonViewRender` are pure and unit-tested.
 */

/** Members shown at once when a node is expanded; more on request. */
const kJsonViewChunk = 200;
/** Search stops after this many matches. */
const kJsonViewMaxMatches = 1000;

type JsonValue = null | boolean | number | string | JsonValue[] | { [key: string]: JsonValue };
type JsonPathPart = string | number;

/** Whether a document with this content type and text is JSON. */
function jsonViewIsJsonDocument(contentType: string, text: string): boolean {
  const type = contentType.toLowerCase().split(';')[0]?.trim() ?? '';
  if (type === 'application/json' || type === 'text/json' || type.endsWith('+json')) {
    return true;
  }
  // "A body that parses as JSON" (006): plain text that starts like an
  // object or array.
  if (type !== 'text/plain') {
    return false;
  }
  const start = text.trimStart();
  return start.startsWith('{') || start.startsWith('[');
}

/** The JavaScript-style path of a value: $.users[0].name, $["a b"]. */
function jsonViewPath(parts: readonly JsonPathPart[]): string {
  let path = '$';
  for (const part of parts) {
    if (typeof part === 'number') {
      path += `[${part}]`;
    } else if (/^[A-Za-z_$][A-Za-z0-9_$]*$/.test(part)) {
      path += `.${part}`;
    } else {
      path += `[${JSON.stringify(part)}]`;
    }
  }
  return path;
}

/** A one-line summary of a collapsed value: {3}, [12], "text". */
function jsonViewSummary(value: JsonValue): string {
  if (Array.isArray(value)) {
    return `[${value.length}]`;
  }
  if (value !== null && typeof value === 'object') {
    return `{${Object.keys(value).length}}`;
  }
  return JSON.stringify(value);
}

/**
 * Paths of keys and values that contain `query` (case-insensitive), in
 * document order, at most `limit`.
 */
function jsonViewSearch(value: JsonValue, query: string, limit: number): JsonPathPart[][] {
  const needle = query.toLowerCase();
  const matches: JsonPathPart[][] = [];
  if (needle === '') {
    return matches;
  }
  const visit = (node: JsonValue, path: JsonPathPart[], key: string | null): void => {
    if (matches.length >= limit) {
      return;
    }
    const keyMatches = key !== null && key.toLowerCase().includes(needle);
    if (node !== null && typeof node === 'object') {
      if (keyMatches) {
        matches.push(path);
      }
      if (Array.isArray(node)) {
        node.forEach((child, index) => visit(child, [...path, index], null));
      } else {
        for (const [childKey, child] of Object.entries(node)) {
          visit(child, [...path, childKey], childKey);
        }
      }
      return;
    }
    if (keyMatches || String(node).toLowerCase().includes(needle)) {
      matches.push(path);
    }
  };
  visit(value, [], null);
  return matches;
}

/** Whether a string value should become a link (http and https only). */
function jsonViewIsLink(text: string): boolean {
  return /^https?:\/\/[^\s]+$/i.test(text) && text.length < 2048;
}

/**
 * The viewer's styles; light and dark like the browser. Everything is
 * scoped to the host element (.bj-host), so a page that shows the viewer
 * next to other content (the Requests tool) keeps its own styles.
 */
const kJsonViewStyle = `
.bj-host { --bg:#fff; --fg:#1d1d1f; --muted:#6e6e73; --line:#d2d2d7;
  --key:#881391; --string:#1a1aa6; --number:#1c00cf; --literal:#0d22aa; --hit:#ffe58f; --current:#ffb340;
  background:var(--bg); color:var(--fg); font:13px/1.5 ui-monospace, Menlo, monospace; }
@media (prefers-color-scheme: dark) { .bj-host { --bg:#1e1e1f; --fg:#f5f5f7; --muted:#a1a1a6; --line:#3d3d40;
  --key:#ee8bf6; --string:#9ecbff; --number:#a3e19f; --literal:#9fb6ff; --hit:#6b5a17; --current:#a0661b; } }
.bj-host .bj-bar { position:sticky; top:0; display:flex; gap:8px; align-items:center; padding:6px 12px;
  background:var(--bg); border-bottom:1px solid var(--line); font:12px -apple-system, sans-serif; z-index:1; }
.bj-host .bj-bar input { font:inherit; padding:3px 6px; border:1px solid var(--line); border-radius:6px;
  background:var(--bg); color:var(--fg); width:220px; }
.bj-host .bj-bar button { font:inherit; padding:2px 8px; border:1px solid var(--line); border-radius:6px;
  background:var(--bg); color:var(--fg); cursor:pointer; }
.bj-host .bj-bar button[aria-pressed=true] { border-color:var(--fg); }
.bj-host .bj-bar .count, .bj-host .bj-bar .note { color:var(--muted); }
.bj-tree, .bj-raw { padding:8px 12px; }
.bj-raw { white-space:pre-wrap; word-break:break-all; margin:0; font:inherit; }
.bj-row { white-space:nowrap; }
.bj-children { padding-left:18px; border-left:1px dotted var(--line); margin-left:5px; }
.bj-toggle { display:inline-block; width:14px; cursor:pointer; color:var(--muted); user-select:none; }
.bj-key { color:var(--key); }
.bj-string { color:var(--string); white-space:pre-wrap; }
.bj-number { color:var(--number); }
.bj-literal { color:var(--literal); }
.bj-summary { color:var(--muted); cursor:pointer; }
.bj-copy { visibility:hidden; margin-left:8px; font:11px -apple-system, sans-serif; color:var(--muted);
  cursor:pointer; border:1px solid var(--line); border-radius:4px; padding:0 4px; background:var(--bg); }
.bj-row:hover > .bj-copy { visibility:visible; }
.bj-more { color:var(--muted); cursor:pointer; text-decoration:underline; font:12px -apple-system, sans-serif; }
.bj-hit { background:var(--hit); }
.bj-current { background:var(--current); }
`;

/** Extra styles when the viewer replaces a whole JSON document. */
const kJsonViewPageStyle = `
:root { color-scheme: light dark; }
body.bj-host { margin:0; }
`;

const jsonViewElement = <K extends keyof HTMLElementTagNameMap>(
  tag: K, className?: string, text?: string): HTMLElementTagNameMap[K] => {
  const node = document.createElement(tag);
  if (className !== undefined) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
};

function jsonViewCopy(text: string): void {
  void navigator.clipboard?.writeText(text).catch(() => undefined);
}

let jsonViewStylesAdded = false;

/** Adds the viewer's styles to the document once. */
function jsonViewAddStyles(page: boolean): void {
  // A constructed style sheet, not a <style> element: pages and internal
  // pages with a Content-Security-Policy (style-src 'self') allow it.
  if (jsonViewStylesAdded) return;
  jsonViewStylesAdded = true;
  const sheet = new CSSStyleSheet();
  sheet.replaceSync(kJsonViewStyle + (page ? kJsonViewPageStyle : ''));
  document.adoptedStyleSheets = [...document.adoptedStyleSheets, sheet];
}

/**
 * Shows `data` (parsed from `rawText`) in `host` as a tree with search and
 * Tree / Raw / Pretty views, replacing the host's content. `page`: the
 * viewer is the whole document.
 */
function jsonViewRender(host: HTMLElement, data: JsonValue, rawText: string, page = false): void {
  jsonViewAddStyles(page);
  host.classList.add('bj-host');
  const element = jsonViewElement;
  const copy = jsonViewCopy;

  const bar = element('div', 'bj-bar');
  const tree = element('div', 'bj-tree');
  const raw = element('pre', 'bj-raw');
  raw.hidden = true;

  const treeButton = element('button', undefined, 'Tree');
  const rawButton = element('button', undefined, 'Raw');
  const prettyButton = element('button', undefined, 'Pretty');
  const search = element('input');
  search.type = 'search';
  search.placeholder = 'Search keys and values';
  search.setAttribute('aria-label', 'Search keys and values');
  const previous = element('button', undefined, '↑');
  previous.title = 'Previous match';
  const next = element('button', undefined, '↓');
  next.title = 'Next match';
  const count = element('span', 'count');
  bar.append(treeButton, rawButton, prettyButton, search, previous, next, count);
  if (page) bar.append(element('span', 'note', 'JSON viewer (Brouk)'));

  // Rows by path, for revealing search matches.
  const rows = new Map<string, HTMLElement>();
  const expanders = new Map<string, () => void>();
  const memberRevealers = new Map<string, (index: number) => void>();

  const keyOf = (path: readonly JsonPathPart[]): string => JSON.stringify(path);

  function valueNode(value: JsonValue): HTMLElement {
    if (typeof value === 'string') {
      // Links only in a JSON document: elsewhere (the Requests tool) they
      // would navigate away from the page showing the viewer.
      if (page && jsonViewIsLink(value)) {
        const link = element('a', 'bj-string', JSON.stringify(value));
        link.href = value; // nosemgrep: ts-url-sink (http(s) only, checked above)
        link.rel = 'noreferrer';
        return link;
      }
      return element('span', 'bj-string', JSON.stringify(value));
    }
    if (typeof value === 'number') return element('span', 'bj-number', String(value));
    return element('span', 'bj-literal', String(value));
  }

  function row(key: JsonPathPart | null, value: JsonValue, path: JsonPathPart[]): HTMLElement {
    const container = element('div');
    const line = element('div', 'bj-row');
    container.append(line);
    rows.set(keyOf(path), line);
    const isContainer = value !== null && typeof value === 'object';
    const toggle = element('span', 'bj-toggle', isContainer ? '▸' : '');
    line.append(toggle);
    if (key !== null) {
      line.append(element('span', 'bj-key', typeof key === 'number' ? String(key) : JSON.stringify(key)), ': ');
    }
    if (!isContainer) {
      line.append(valueNode(value));
    } else {
      const summary = element('span', 'bj-summary', jsonViewSummary(value));
      line.append(summary);
      let children: HTMLElement | null = null;
      const expand = (): void => {
        if (children !== null) {
          children.hidden = false;
          toggle.textContent = '▾';
          return;
        }
        children = element('div', 'bj-children');
        const entries: [JsonPathPart, JsonValue][] = Array.isArray(value)
          ? value.map((child, index): [JsonPathPart, JsonValue] => [index, child])
          : Object.entries(value);
        const target = children;
        // The rendered members are entries[first, shown); long lists are
        // shown in windows, so revealing a match far down stays quick.
        let first = 0;
        let shown = 0;
        const before = element('div', 'bj-more');
        const more = element('div', 'bj-more');
        const rowsFor = (from: number, to: number): DocumentFragment => {
          const fragment = document.createDocumentFragment();
          for (let i = from; i < to; ++i) {
            const entry = entries[i];
            if (entry !== undefined) fragment.append(row(entry[0], entry[1], [...path, entry[0]]));
          }
          return fragment;
        };
        const updateLinks = (): void => {
          before.remove();
          more.remove();
          if (first > 0) {
            before.textContent = `Show ${Math.min(kJsonViewChunk, first)} previous of ${first}`;
            target.prepend(before);
          }
          if (shown < entries.length) {
            more.textContent = `Show ${Math.min(kJsonViewChunk, entries.length - shown)} more of ${entries.length - shown}`;
            target.append(more);
          }
        };
        const showMore = (): void => {
          const end = Math.min(entries.length, shown + kJsonViewChunk);
          more.remove();
          target.append(rowsFor(shown, end));
          shown = end;
          updateLinks();
        };
        const showPrevious = (): void => {
          const start = Math.max(0, first - kJsonViewChunk);
          before.remove();
          target.prepend(rowsFor(start, first));
          first = start;
          updateLinks();
        };
        more.addEventListener('click', showMore);
        before.addEventListener('click', showPrevious);
        showMore();
        container.append(children);
        toggle.textContent = '▾';
        // Revealing a member: if it is not rendered, show a window around it.
        memberRevealers.set(keyOf(path), (index: number) => {
          if (index >= first && index < shown) return;
          target.replaceChildren();
          first = Math.max(0, index - kJsonViewChunk / 2);
          shown = first;
          showMore();
        });
      };
      const toggleOpen = (): void => {
        if (children !== null && !children.hidden) {
          children.hidden = true;
          toggle.textContent = '▸';
        } else {
          expand();
        }
      };
      toggle.addEventListener('click', toggleOpen);
      summary.addEventListener('click', toggleOpen);
      expanders.set(keyOf(path), expand);
      // The first two levels start open, as in Firefox's viewer.
      if (path.length < 2) expand();
    }
    const copyValue = element('span', 'bj-copy', 'Copy value');
    copyValue.addEventListener('click', () =>
      copy(typeof value === 'string' ? value : JSON.stringify(value, null, 2)));
    const copyPath = element('span', 'bj-copy', 'Copy path');
    copyPath.addEventListener('click', () => copy(jsonViewPath(path)));
    line.append(copyValue, copyPath);
    return container;
  }

  tree.append(row(null, data, []));

  const valueAt = (path: readonly JsonPathPart[]): JsonValue => {
    let value: JsonValue = data;
    for (const part of path) {
      if (Array.isArray(value)) value = value[Number(part)] ?? null;
      else if (value !== null && typeof value === 'object') value = value[String(part)] ?? null;
      else return null;
    }
    return value;
  };

  // Search.
  let matches: JsonPathPart[][] = [];
  let current = -1;
  const reveal = (path: JsonPathPart[]): HTMLElement | undefined => {
    for (let depth = 0; depth < path.length; ++depth) {
      const parent = path.slice(0, depth);
      expanders.get(keyOf(parent))?.();
      // The member's position among its parent's members.
      const member = path[depth];
      const parentValue = valueAt(parent);
      const index = Array.isArray(parentValue)
        ? Number(member)
        : parentValue !== null && typeof parentValue === 'object'
          ? Object.keys(parentValue).indexOf(String(member))
          : -1;
      if (index >= 0) memberRevealers.get(keyOf(parent))?.(index);
    }
    return rows.get(keyOf(path));
  };
  const mark = (): void => {
    host.querySelectorAll('.bj-hit, .bj-current').forEach((node) => {
      node.classList.remove('bj-hit', 'bj-current');
    });
    const target = current >= 0 ? matches[current] : undefined;
    if (target === undefined) {
      count.textContent = matches.length === 0 && search.value !== '' ? 'No matches' : '';
      return;
    }
    const line = reveal(target);
    line?.classList.add('bj-current');
    line?.scrollIntoView({ block: 'center' });
    count.textContent = `${current + 1} of ${matches.length}${matches.length >= kJsonViewMaxMatches ? '+' : ''}`;
  };
  let timer = 0;
  search.addEventListener('input', () => {
    window.clearTimeout(timer);
    timer = window.setTimeout(() => {
      matches = jsonViewSearch(data, search.value, kJsonViewMaxMatches);
      current = matches.length > 0 ? 0 : -1;
      mark();
    }, 200);
  });
  const step = (delta: number): void => {
    if (matches.length === 0) return;
    current = (current + delta + matches.length) % matches.length;
    mark();
  };
  next.addEventListener('click', () => step(1));
  previous.addEventListener('click', () => step(-1));
  search.addEventListener('keydown', (event) => {
    if (event.key === 'Enter') step(event.shiftKey ? -1 : 1);
  });

  // Views.
  const show = (view: 'tree' | 'raw' | 'pretty'): void => {
    tree.hidden = view !== 'tree';
    raw.hidden = view === 'tree';
    if (view === 'raw') raw.textContent = rawText;
    if (view === 'pretty') raw.textContent = JSON.stringify(data, null, 2);
    treeButton.setAttribute('aria-pressed', String(view === 'tree'));
    rawButton.setAttribute('aria-pressed', String(view === 'raw'));
    prettyButton.setAttribute('aria-pressed', String(view === 'pretty'));
  };
  treeButton.addEventListener('click', () => show('tree'));
  rawButton.addEventListener('click', () => show('raw'));
  prettyButton.addEventListener('click', () => show('pretty'));
  show('tree');

  host.replaceChildren(bar, tree, raw);
}

function broukJsonViewerMain(): void {
  const start = (): void => {
    // Chromium shows JSON and plain text as a <pre> in the body (JSON also
    // with its own "Pretty-print" checkbox above it).
    const pre = document.body?.querySelector(':scope > pre');
    const text = pre?.textContent ?? '';
    if (pre === null || pre === undefined || !jsonViewIsJsonDocument(document.contentType, text)) {
      return;
    }
    let data: JsonValue;
    try {
      data = JSON.parse(text) as JsonValue;
    } catch {
      return;  // Not JSON after all: leave the page as it is.
    }
    jsonViewRender(document.body, data, text, true);
  };

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', start, { once: true });
  } else {
    start();
  }
}

// Only in a document (the unit tests load this file without one).
if (typeof document !== 'undefined') {
  broukJsonViewerMain();
}
