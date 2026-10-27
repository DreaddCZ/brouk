/**
 * The task manager (020), brouk://task-manager in its own window. Asks the
 * browser for its tasks once per refresh while open; the browser measures
 * nothing while it is closed.
 */
import { cefTransport } from '../shared/bridge.js';
import { byId, element, errorText } from '../shared/dom.js';
import { wireHelpButtons } from '../shared/help.js';
import {
  act,
  copyText,
  endTasks,
  loadList,
  loadSettings,
  saveSetting,
  type PageAction,
  type TaskList,
  type TaskSettings,
} from './api.js';
import {
  cellText,
  columnById,
  formatBytes,
  formatColumns,
  formatCpu,
  formatSort,
  History,
  kColumns,
  nearestSample,
  nextSort,
  niceBytes,
  niceMax,
  otherPagesInProcesses,
  parseColumns,
  parseSort,
  rowTitle,
  statusWords,
  toCsv,
  toJson,
  viewRows,
  type ColumnId,
  type Sort,
  type ViewRow,
} from './logic.js';

const transport = cefTransport();
const head = byId<HTMLTableRowElement>('head');
const body = byId<HTMLTableSectionElement>('rows');
const totals = byId<HTMLTableRowElement>('totals');
const filter = byId<HTMLInputElement>('filter');
const freeze = byId<HTMLButtonElement>('freeze');
const status = byId<HTMLSpanElement>('status');
const errorBox = byId<HTMLParagraphElement>('error');
const columnMenu = byId<HTMLDivElement>('column-menu');
const graphPane = byId<HTMLDetailsElement>('graph-pane');
const graph = byId<HTMLDivElement>('graph');
const graphEmpty = byId<HTMLParagraphElement>('graph-empty');
const confirmDialog = byId<HTMLDialogElement>('confirm-end');

let settings: TaskSettings = {
  columns: '',
  sort: '',
  refresh: 1,
  history: 5,
  graph: true,
};
let columns: ColumnId[] = parseColumns('');
let sort: Sort = parseSort('');
let list: TaskList | null = null;
let views: ViewRow[] = [];
let frozen = false;
let timer: number | undefined;
const history = new History(5 * 60_000);
const selected = new Set<string>();
/** The row with the keyboard focus (roving tabindex), and the anchor for Shift-click. */
let current: string | null = null;
let anchor: string | null = null;
/** Selected when it appears (Show in Task Manager, before the first list). */
let pendingSelect: string | null = null;
const rowElements = new Map<string, HTMLTableRowElement>();

// ---- Loading ----

function showError(error: unknown): void {
  errorBox.textContent = errorText(error);
  errorBox.hidden = false;
}

async function refreshSettings(): Promise<void> {
  try {
    settings = await loadSettings(transport);
    columns = parseColumns(settings.columns);
    sort = parseSort(settings.sort);
    history.setLength(settings.history * 60_000);
    graphPane.open = settings.graph;
    renderHead();
    render();
  } catch (error) {
    showError(error);
  }
}

async function refresh(): Promise<void> {
  window.clearTimeout(timer);
  timer = undefined;
  if (frozen) return;
  try {
    list = await loadList(transport, columns.includes('js_memory'));
    errorBox.hidden = true;
    history.add(list);
    if (pendingSelect !== null && list.rows.some((row) => row.key === pendingSelect)) {
      selectOnly(pendingSelect);
      pendingSelect = null;
      rowElements.get(current ?? '')?.scrollIntoView({ block: 'nearest' });
    }
    render();
  } catch (error) {
    showError(error);
  }
  if (!frozen) timer = window.setTimeout(() => void refresh(), settings.refresh * 1000);
}

// ---- Table ----

function renderHead(): void {
  head.replaceChildren();
  for (const id of columns) {
    const column = columnById(id);
    const th = element('th');
    th.scope = 'col';
    if (column.numeric) th.className = 'numeric';
    if (sort.column === id) th.setAttribute('aria-sort', sort.descending ? 'descending' : 'ascending');
    const sortButton = element('button', { text: column.label });
    sortButton.type = 'button';
    if (column.tooltip !== undefined) sortButton.title = column.tooltip;
    sortButton.addEventListener('click', () => {
      sort = nextSort(sort, id);
      void saveSetting(transport, 'sort', formatSort(sort)).catch(showError);
      renderHead();
      render();
    });
    th.append(sortButton);
    head.append(th);
  }
}

const kGlyphs: Record<string, string> = {
  browser: '◆',
  gpu: '▣',
  utility: '⚙',
  worker: '⚙',
  subframe: '▢',
  other: '▢',
  brouk: '◆',
  tab: '◌',
};

function taskCell(view: ViewRow): HTMLTableCellElement {
  const td = element('td', { className: 'task' });
  const wrap = element('div', { className: 'task-cell' });
  const icon = view.isPage ? view.page?.icon : '';
  if (icon !== undefined && icon !== '') {
    const img = element('img');
    img.src = icon;
    img.alt = '';
    wrap.append(img);
  } else {
    const glyph = element('span', { className: 'glyph', text: kGlyphs[view.row.type] ?? '▢' });
    glyph.setAttribute('aria-hidden', 'true');
    wrap.append(glyph);
  }
  wrap.append(element('span', { className: 'name', text: cellText(view, 'task') }));
  td.title = view.isPage && view.page !== null ? `${rowTitle(view)}\n${view.page.url}` : rowTitle(view);
  td.append(wrap);
  return td;
}

const kStatusIcons: Record<string, string> = {
  'not loaded': '◌',
  crashed: '⚠︎',
  paused: '⏸︎',
  loading: '↻',
  muted: '🔇',
  'playing audio': '🔊',
  camera: '📷',
  microphone: '🎙',
  'sharing screen': '🖥',
  'DevTools open': '🛠',
};

function statusCell(view: ViewRow): HTMLTableCellElement {
  const td = element('td');
  const words = statusWords(view);
  if (words.length === 0) return td;
  const icons = element('span', { className: 'status-icons', text: words.map((w) => kStatusIcons[w] ?? '').join('') });
  icons.setAttribute('aria-hidden', 'true');
  td.title = words.join(', ');
  td.append(icons, element('span', { className: 'visually-hidden', text: words.join(', ') }));
  return td;
}

function profileCell(view: ViewRow): HTMLTableCellElement {
  const td = element('td');
  if (!view.isPage || view.page === null || view.page.profile === '') return td;
  const dot = element('span', { className: 'profile-dot' });
  dot.style.setProperty('--c', `var(--${/^[a-z]+$/.test(view.page.color) ? view.page.color : 'grey'})`);
  dot.setAttribute('aria-hidden', 'true');
  td.append(dot, view.page.profile);
  return td;
}

function cells(view: ViewRow): HTMLTableCellElement[] {
  return columns.map((id) => {
    if (id === 'task') return taskCell(view);
    if (id === 'status') return statusCell(view);
    if (id === 'profile') return profileCell(view);
    const td = element('td', { text: cellText(view, id) });
    if (columnById(id).numeric) td.className = 'numeric';
    return td;
  });
}

/** A row's content, to update only rows that changed. */
function signature(view: ViewRow): string {
  return JSON.stringify([
    columns.map((id) => cellText(view, id)),
    view.page?.icon.length ?? 0,
    view.page?.color ?? '',
  ]);
}

function render(): void {
  if (list === null) return;
  views = viewRows(list, sort, filter.value);
  const present = new Set(list.rows.map((row) => row.key));
  for (const key of [...selected]) if (!present.has(key)) selected.delete(key);
  const shownKeys = new Set(views.map((view) => view.row.key));
  if (current !== null && !shownKeys.has(current)) current = views[0]?.row.key ?? null;
  if (current === null) current = views[0]?.row.key ?? null;

  const focused = document.activeElement instanceof HTMLTableRowElement ? document.activeElement : null;
  for (const [key, tr] of rowElements) {
    if (!shownKeys.has(key)) {
      tr.remove();
      rowElements.delete(key);
    }
  }
  let previousProcess: string | null = null;
  views.forEach((view, index) => {
    const key = view.row.key;
    let tr = rowElements.get(key);
    if (tr === undefined) {
      tr = element('tr');
      tr.dataset['key'] = key;
      rowElements.set(key, tr);
    }
    const sig = signature(view);
    if (tr.dataset['sig'] !== sig) {
      tr.replaceChildren(...cells(view));
      tr.dataset['sig'] = sig;
    }
    const group = view.row.process === null ? `page-${key}` : `p-${view.row.process}`;
    tr.classList.toggle('group-start', group !== previousProcess && index > 0);
    tr.classList.toggle('member', !view.first);
    tr.classList.toggle('not-loaded', view.row.process === null);
    previousProcess = group;
    tr.setAttribute('aria-selected', String(selected.has(key)));
    tr.tabIndex = key === current ? 0 : -1;
    // Moved only when out of place, so focus and VoiceOver stay put.
    if (body.children[index] !== tr) body.insertBefore(tr, body.children[index] ?? null);
  });
  if (focused !== null && focused.isConnected) focused.focus({ preventScroll: true });

  totals.replaceChildren(
    ...columns.map((id) => {
      if (id === 'task') return element('td', { text: 'Total (Brouk)' });
      if (id === 'memory') return element('td', { className: 'numeric', text: formatBytes(list?.totalMemory ?? -1) });
      if (id === 'cpu') return element('td', { className: 'numeric', text: formatCpu(list?.totalCpu ?? 0) });
      return element('td');
    }),
  );
  updateActions();
  renderGraph();
}

// ---- Selection and keyboard ----

function selectOnly(key: string): void {
  selected.clear();
  selected.add(key);
  current = key;
  anchor = key;
}

function selectRange(from: string, to: string): void {
  const keys = views.map((view) => view.row.key);
  const a = keys.indexOf(from);
  const b = keys.indexOf(to);
  if (a < 0 || b < 0) {
    selectOnly(to);
    return;
  }
  selected.clear();
  for (let i = Math.min(a, b); i <= Math.max(a, b); i++) selected.add(keys[i] ?? '');
  current = to;
}

body.addEventListener('mousedown', (event) => {
  const tr = (event.target as Element).closest('tr');
  const key = tr?.dataset['key'];
  if (key === undefined) return;
  if (event.shiftKey && anchor !== null) {
    selectRange(anchor, key);
  } else if (event.metaKey) {
    if (selected.has(key)) selected.delete(key);
    else selected.add(key);
    current = key;
    anchor = key;
  } else {
    selectOnly(key);
  }
  render();
  rowElements.get(key)?.focus({ preventScroll: true });
});

body.addEventListener('dblclick', (event) => {
  const key = (event.target as Element).closest('tr')?.dataset['key'];
  if (key !== undefined) void runAction('goto');
});

function moveFocus(delta: number, extend: boolean): void {
  if (views.length === 0) return;
  const keys = views.map((view) => view.row.key);
  const index = current === null ? -1 : keys.indexOf(current);
  const next = keys[Math.max(0, Math.min(keys.length - 1, index + delta))] ?? null;
  if (next === null) return;
  if (extend && anchor !== null) selectRange(anchor, next);
  else selectOnly(next);
  render();
  const tr = rowElements.get(next);
  tr?.focus();
  tr?.scrollIntoView({ block: 'nearest' });
}

body.addEventListener('keydown', (event) => {
  if (event.target instanceof HTMLInputElement) return;
  const plain = !event.metaKey && !event.ctrlKey && !event.altKey;
  if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
    moveFocus(event.key === 'ArrowDown' ? 1 : -1, event.shiftKey);
  } else if (event.key === 'Home' || event.key === 'End') {
    moveFocus(event.key === 'Home' ? -views.length : views.length, event.shiftKey);
  } else if (event.key === 'Enter' && plain) {
    void runAction('goto');
  } else if (event.key === 'Backspace' && event.metaKey) {
    void runAction('end');
  } else if (event.key === 'a' && event.metaKey) {
    for (const view of views) selected.add(view.row.key);
    render();
  } else if (event.key === 'c' && event.metaKey) {
    void copy('csv');
  } else if (plain && ['p', 'u', 'm', 'r', 'd'].includes(event.key)) {
    const actions: Record<string, string> = { p: 'pause', u: 'unload', m: 'mute', r: 'reload', d: 'devtools' };
    void runAction(actions[event.key] ?? '');
  } else {
    return;
  }
  event.preventDefault();
});

/** "Show in Task Manager": called by the browser with the tab's row. */
function selectTaskRow(key: unknown): void {
  if (typeof key !== 'string') return;
  if (list?.rows.some((row) => row.key === key) === true) {
    selectOnly(key);
    render();
    const tr = rowElements.get(key);
    tr?.focus();
    tr?.scrollIntoView({ block: 'nearest' });
  } else {
    pendingSelect = key;
  }
}
Reflect.set(globalThis, 'selectTaskRow', selectTaskRow);

// ---- Actions ----

function selectedViews(): ViewRow[] {
  return views.filter((view) => selected.has(view.row.key));
}

/** The pages an action applies to: selected pages, or the page a worker or subframe belongs to. */
function selectedPages(): string[] {
  const pages = new Set<string>();
  for (const view of selectedViews()) if (view.row.page !== null) pages.add(view.row.page);
  return [...pages];
}

function tabPages(): Array<{ key: string; info: NonNullable<ViewRow['page']> }> {
  const result: Array<{ key: string; info: NonNullable<ViewRow['page']> }> = [];
  for (const key of selectedPages()) {
    const info = list?.pages.get(key);
    if (info !== undefined && !info.ownWindow) result.push({ key, info });
  }
  return result;
}

function actionButton(name: string): HTMLButtonElement {
  return document.querySelector<HTMLButtonElement>(`[data-action="${name}"]`) as HTMLButtonElement;
}

function updateActions(): void {
  const pages = tabPages();
  const web = pages.filter((page) => !page.info.brouk);
  const loadedWeb = web.filter((page) => page.info.loaded);
  actionButton('goto').disabled = selectedPages().length !== 1;
  actionButton('end').disabled = !selectedViews().some((view) => view.row.killable && view.row.task !== null);
  const pause = actionButton('pause');
  const allPaused = loadedWeb.length > 0 && loadedWeb.every((page) => page.info.paused);
  pause.textContent = allPaused ? 'Resume' : 'Pause';
  pause.disabled = loadedWeb.filter((page) => allPaused || !page.info.crashed).length === 0;
  const unload = actionButton('unload');
  unload.disabled = !loadedWeb.some((page) => !page.info.shown);
  unload.title = loadedWeb.length > 0 && loadedWeb.every((page) => page.info.shown)
    ? 'The tab shown in its window cannot be unloaded'
    : "Free the tab's memory; it loads again when shown (U)";
  const mute = actionButton('mute');
  const allMuted = loadedWeb.length > 0 && loadedWeb.every((page) => page.info.muted);
  mute.textContent = allMuted ? 'Unmute' : 'Mute';
  mute.disabled = loadedWeb.length === 0;
  actionButton('reload').disabled = pages.filter((page) => page.info.loaded).length === 0;
  actionButton('devtools').disabled = web.length !== 1;
}

async function runAction(name: string): Promise<void> {
  try {
    if (name === 'end') {
      await endSelected();
    } else if (name === 'goto') {
      const page = selectedPages()[0];
      if (selectedPages().length === 1 && page !== undefined) await act(transport, 'goto', [page]);
    } else {
      const button = actionButton(name);
      if (button.disabled) return;
      const pages = tabPages().map((page) => page.key);
      let action = name as PageAction;
      if (name === 'pause' && button.textContent === 'Resume') action = 'resume';
      if (name === 'mute' && button.textContent === 'Unmute') action = 'unmute';
      if (name === 'devtools' && pages.length !== 1) return;
      await act(transport, action, pages);
    }
  } catch (error) {
    showError(error);
  }
  if (!frozen) void refresh();
}

async function endSelected(): Promise<void> {
  if (list === null) return;
  const chosen = selectedViews().filter((view) => view.row.killable && view.row.task !== null);
  if (chosen.length === 0) return;
  const keys = new Set(chosen.map((view) => view.row.key));
  const others = otherPagesInProcesses(list, keys);
  if (others.length > 0) {
    byId('confirm-text').textContent =
      'The process also runs these tabs. Each of them will show that it crashed, with a Reload button:';
    byId('confirm-list').replaceChildren(...others.map((title) => element('li', { text: title })));
    confirmDialog.returnValue = '';
    confirmDialog.showModal();
    const answer = await new Promise<string>((resolve) => {
      confirmDialog.addEventListener('close', () => resolve(confirmDialog.returnValue), { once: true });
    });
    if (answer !== 'end') return;
  }
  await endTasks(
    transport,
    chosen.map((view) => view.row.task ?? 0),
  );
}

for (const button of document.querySelectorAll<HTMLButtonElement>('[data-action]')) {
  button.addEventListener('click', () => void runAction(button.dataset['action'] ?? ''));
}

// ---- Pause updates, filter, copy ----

freeze.addEventListener('click', () => {
  frozen = !frozen;
  freeze.setAttribute('aria-pressed', String(frozen));
  freeze.textContent = frozen ? 'Resume updates' : 'Pause updates';
  status.textContent = frozen ? 'Values are not updated.' : '';
  if (!frozen) void refresh();
  else window.clearTimeout(timer);
});

filter.addEventListener('input', () => render());

async function copy(format: 'csv' | 'json'): Promise<void> {
  if (list === null) return;
  const chosen = selected.size > 0 ? selectedViews() : views;
  const text =
    format === 'csv'
      ? toCsv(chosen, columns, Date.now())
      : toJson(chosen, columns, Date.now(), (key) => history.samples(key));
  try {
    await copyText(transport, text);
    status.textContent = `Copied ${chosen.length} ${chosen.length === 1 ? 'row' : 'rows'} as ${format.toUpperCase()}.`;
  } catch (error) {
    showError(error);
  }
}
byId('copy-csv').addEventListener('click', () => void copy('csv'));
byId('copy-json').addEventListener('click', () => void copy('json'));

// ---- Column picker (right-click the header) ----

function closeColumnMenu(): void {
  columnMenu.hidden = true;
}

head.addEventListener('contextmenu', (event) => {
  event.preventDefault();
  columnMenu.replaceChildren();
  for (const column of kColumns) {
    const label = element('label');
    label.setAttribute('role', 'menuitemcheckbox');
    const box = element('input');
    box.type = 'checkbox';
    box.checked = columns.includes(column.id);
    box.disabled = column.id === 'task';
    label.setAttribute('aria-checked', String(box.checked));
    box.addEventListener('change', () => {
      if (box.checked) {
        // In the order of the full list.
        const chosen = new Set([...columns, column.id]);
        columns = kColumns.map((c) => c.id).filter((id) => chosen.has(id));
      } else {
        columns = columns.filter((id) => id !== column.id);
      }
      label.setAttribute('aria-checked', String(box.checked));
      void saveSetting(transport, 'columns', formatColumns(columns)).catch(showError);
      for (const tr of rowElements.values()) delete tr.dataset['sig'];
      renderHead();
      render();
      if (column.id === 'js_memory' && box.checked) void refresh();
    });
    label.append(box, column.label);
    columnMenu.append(label);
  }
  columnMenu.hidden = false;
  const width = columnMenu.offsetWidth;
  const height = columnMenu.offsetHeight;
  columnMenu.style.left = `${Math.min(event.clientX, window.innerWidth - width - 8)}px`;
  columnMenu.style.top = `${Math.min(event.clientY, window.innerHeight - height - 8)}px`;
  columnMenu.querySelector('input:not(:disabled)')?.dispatchEvent(new Event('focus'));
  (columnMenu.querySelector('input:not(:disabled)') as HTMLInputElement | null)?.focus();
});
document.addEventListener('mousedown', (event) => {
  if (!columnMenu.hidden && !columnMenu.contains(event.target as Node)) closeColumnMenu();
});
document.addEventListener('keydown', (event) => {
  if (event.key === 'Escape' && !columnMenu.hidden) {
    closeColumnMenu();
    event.preventDefault();
  }
});

// ---- Graph ----

const kLineColors = ['var(--blue)', 'var(--orange)', 'var(--green)', 'var(--purple)', 'var(--red)', 'var(--cyan)',
  'var(--pink)', 'var(--yellow)'];
const kSvg = 'http://www.w3.org/2000/svg';

function svg<K extends keyof SVGElementTagNameMap>(tag: K, attributes: Record<string, string | number>): SVGElementTagNameMap[K] {
  const node = document.createElementNS(kSvg, tag);
  for (const [name, value] of Object.entries(attributes)) node.setAttribute(name, String(value));
  return node;
}

graphPane.addEventListener('toggle', () => {
  if (graphPane.open !== settings.graph) {
    settings = { ...settings, graph: graphPane.open };
    void saveSetting(transport, 'graph', graphPane.open).catch(showError);
  }
  renderGraph();
});

let tip: HTMLDivElement | null = null;

function renderGraph(): void {
  if (!graphPane.open || list === null) return;
  const chosen = selectedViews().slice(0, kLineColors.length);
  graphEmpty.hidden = chosen.length > 0;
  graph.hidden = chosen.length === 0;
  if (chosen.length === 0) {
    graph.replaceChildren();
    return;
  }
  const width = Math.max(graph.clientWidth, 200);
  const height = 150;
  const left = 46;
  const right = 60;
  const top = 8;
  const bottom = 18;
  const end = list.time;
  const start = end - settings.history * 60_000;
  const series = chosen.map((view) => history.samples(view.row.key));
  const maxCpu = niceMax(Math.max(1, ...series.flat().map((s) => s.cpu)));
  const maxMemory = niceBytes(Math.max(1, ...series.flat().map((s) => s.memory)));
  const x = (time: number): number => left + ((time - start) / (end - start)) * (width - left - right);
  const yCpu = (cpu: number): number => top + (1 - cpu / maxCpu) * (height - top - bottom);
  const yMemory = (bytes: number): number => top + (1 - bytes / maxMemory) * (height - top - bottom);

  const root = svg('svg', { viewBox: `0 0 ${width} ${height}`, role: 'img' });
  root.setAttribute('aria-label', 'CPU and memory of the selected rows over time');
  root.append(
    svg('line', { class: 'axis', x1: left, y1: height - bottom, x2: width - right, y2: height - bottom }),
    svg('line', { class: 'axis', x1: left, y1: top, x2: left, y2: height - bottom }),
    svg('line', { class: 'axis', x1: width - right, y1: top, x2: width - right, y2: height - bottom }),
  );
  const label = (text: string, lx: number, ly: number, anchorAt: string): void => {
    const node = svg('text', { class: 'label', x: lx, y: ly, 'text-anchor': anchorAt });
    node.textContent = text;
    root.append(node);
  };
  label(formatCpu(maxCpu).replace('.0', ''), left - 4, top + 8, 'end');
  label('0 %', left - 4, height - bottom, 'end');
  label(formatBytes(maxMemory), width - right + 4, top + 8, 'start');
  label('0', width - right + 4, height - bottom, 'start');
  label(`${settings.history} min ago`, left, height - 4, 'start');
  label('now', width - right, height - 4, 'end');
  series.forEach((samples, index) => {
    const color = kLineColors[index] ?? 'var(--blue)';
    const shown = samples.filter((s) => s.time >= start);
    if (shown.length === 0) return;
    const cpuPath = shown.map((s, i) => `${i === 0 ? 'M' : 'L'}${x(s.time).toFixed(1)},${yCpu(s.cpu).toFixed(1)}`);
    const memoryPath = shown.map(
      (s, i) => `${i === 0 ? 'M' : 'L'}${x(s.time).toFixed(1)},${yMemory(s.memory).toFixed(1)}`,
    );
    root.append(
      svg('path', { d: cpuPath.join(''), fill: 'none', stroke: color, 'stroke-width': 1.5 }),
      svg('path', { d: memoryPath.join(''), fill: 'none', stroke: color, 'stroke-width': 1.5, class: 'memory' }),
    );
  });
  const cursor = svg('line', { class: 'cursor', x1: 0, y1: top, x2: 0, y2: height - bottom, visibility: 'hidden' });
  root.append(cursor);

  const legend = element('div', { className: 'legend' });
  chosen.forEach((view, index) => {
    const item = element('span');
    const swatch = element('i');
    swatch.style.background = kLineColors[index] ?? 'var(--blue)';
    item.append(swatch, rowTitle(view));
    legend.append(item);
  });
  legend.append(element('span', { text: 'solid: CPU (left scale) · dashed: memory (right scale)' }));
  tip = element('div', { className: 'graph-tip' });
  tip.hidden = true;
  graph.replaceChildren(root, legend, tip);

  root.addEventListener('mousemove', (event) => {
    const box = root.getBoundingClientRect();
    const px = ((event.clientX - box.left) / box.width) * width;
    const time = start + ((px - left) / (width - left - right)) * (end - start);
    if (px < left || px > width - right || tip === null) return;
    cursor.setAttribute('x1', String(px));
    cursor.setAttribute('x2', String(px));
    cursor.setAttribute('visibility', 'visible');
    const lines = [new Date(time).toLocaleTimeString()];
    chosen.forEach((view, index) => {
      const sample = nearestSample(series[index] ?? [], time);
      if (sample !== null) {
        lines.push(`${rowTitle(view)}: ${formatCpu(sample.cpu)}, ${formatBytes(sample.memory)}`);
      }
    });
    tip.textContent = lines.join('\n');
    tip.style.whiteSpace = 'pre';
    tip.hidden = false;
    tip.style.left = `${Math.min(event.clientX - box.left + 12, box.width - tip.offsetWidth - 4)}px`;
    tip.style.top = '4px';
  });
  root.addEventListener('mouseleave', () => {
    cursor.setAttribute('visibility', 'hidden');
    if (tip !== null) tip.hidden = true;
  });
}

window.addEventListener('resize', () => renderGraph());

// ---- Start ----

wireHelpButtons();
// Settings may have changed in the file meanwhile (004: live).
window.addEventListener('focus', () => void refreshSettings());
{
  const hash = decodeURIComponent(location.hash.slice(1));
  if (/^tab-\d+$/.test(hash)) pendingSelect = hash;
}
void refreshSettings().then(() => refresh());
