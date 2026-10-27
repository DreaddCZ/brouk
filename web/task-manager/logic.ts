/**
 * The task manager's table (020): columns, sorting with process groups,
 * filtering, values as text, copying and the graph's history. Pure
 * functions, tested with Node.
 */
import type { PageInfo, TaskList, TaskRow } from './api.js';

export type ColumnId =
  | 'task'
  | 'profile'
  | 'memory'
  | 'cpu'
  | 'status'
  | 'pid'
  | 'gpu'
  | 'js_memory'
  | 'network'
  | 'address'
  | 'type'
  | 'shared'
  | 'window';

export interface Column {
  readonly id: ColumnId;
  readonly label: string;
  /** Numbers sort largest first when first clicked, and align right. */
  readonly numeric: boolean;
  readonly tooltip?: string;
}

export const kColumns: readonly Column[] = [
  { id: 'task', label: 'Task', numeric: false },
  { id: 'profile', label: 'Profile', numeric: false },
  { id: 'memory', label: 'Memory', numeric: true, tooltip: 'Memory footprint of the process, as in Activity Monitor' },
  { id: 'cpu', label: 'CPU', numeric: true, tooltip: 'CPU use of the process; 100 % is one full core' },
  { id: 'status', label: 'Status', numeric: false },
  { id: 'pid', label: 'Process ID', numeric: true },
  { id: 'gpu', label: 'GPU memory', numeric: true },
  { id: 'js_memory', label: 'JavaScript memory', numeric: true, tooltip: 'JavaScript heap used / allocated' },
  {
    id: 'network',
    label: 'Network',
    numeric: true,
    tooltip: 'Data received per second (approximate: responses from the cache or a service worker may be missing)',
  },
  { id: 'address', label: 'Address', numeric: false },
  { id: 'type', label: 'Type', numeric: false },
  { id: 'shared', label: 'Shared with', numeric: true, tooltip: 'Other tabs in the same process' },
  { id: 'window', label: 'Window', numeric: false },
];

export const kDefaultColumns: readonly ColumnId[] = ['task', 'profile', 'memory', 'cpu', 'status', 'pid'];

export function columnById(id: ColumnId): Column {
  const column = kColumns.find((c) => c.id === id);
  if (column === undefined) throw new Error(`unknown column ${id}`);
  return column;
}

function isColumnId(text: string): text is ColumnId {
  return kColumns.some((c) => c.id === text);
}

/** The setting's columns ("task, memory"), known ones once each; Task always first. */
export function parseColumns(setting: string): ColumnId[] {
  const ids: ColumnId[] = ['task'];
  for (const part of setting.split(',')) {
    const id = part.trim().toLowerCase();
    if (isColumnId(id) && !ids.includes(id)) ids.push(id);
  }
  return ids.length > 1 ? ids : [...kDefaultColumns];
}

export function formatColumns(ids: readonly ColumnId[]): string {
  return ids.join(', ');
}

export interface Sort {
  readonly column: ColumnId;
  readonly descending: boolean;
}

export const kDefaultSort: Sort = { column: 'memory', descending: true };

/** "memory, descending" */
export function parseSort(setting: string): Sort {
  const [name = '', order = ''] = setting.split(',').map((part) => part.trim().toLowerCase());
  if (!isColumnId(name)) return kDefaultSort;
  if (order === 'ascending') return { column: name, descending: false };
  if (order === 'descending') return { column: name, descending: true };
  return { column: name, descending: columnById(name).numeric };
}

export function formatSort(sort: Sort): string {
  return `${sort.column}, ${sort.descending ? 'descending' : 'ascending'}`;
}

/** Clicking a header: the same column turns around, another starts largest (numbers) or A first. */
export function nextSort(current: Sort, column: ColumnId): Sort {
  if (current.column === column) return { column, descending: !current.descending };
  return { column, descending: columnById(column).numeric };
}

// ---- Values ----

const kTypeNames: Record<TaskRow['type'], string> = {
  tab: 'Tab',
  brouk: 'Brouk page',
  subframe: 'Subframe',
  worker: 'Worker',
  browser: 'Browser',
  gpu: 'GPU',
  utility: 'Utility',
  other: 'Other',
};

/** A row and what it shows. */
export interface ViewRow {
  readonly row: TaskRow;
  readonly page: PageInfo | null;
  /** The page is this row (not a worker or subframe of it). */
  readonly isPage: boolean;
  /** Rows of its process (1 for a process of its own or a page not loaded). */
  readonly groupSize: number;
  /** The first row of its process group: process values are shown there. */
  readonly first: boolean;
  /** Other tabs in the same process. */
  readonly shared: number;
}

export function rowTitle(view: ViewRow): string {
  const { row, page } = view;
  if (view.isPage && page !== null) {
    if (page.ownWindow) return page.title === '' ? row.title : page.title;
    return page.title === '' ? page.url : page.title;
  }
  switch (row.type) {
    case 'browser':
      return 'Brouk';
    case 'gpu':
      return 'GPU process';
    default:
      return row.title;
  }
}

export function typeName(view: ViewRow): string {
  if (view.isPage && view.page?.ownWindow === true) return 'Brouk page';
  return kTypeNames[view.row.type];
}

/** The status as words, for the column's text, VoiceOver and copying. */
export function statusWords(view: ViewRow): string[] {
  const page = view.page;
  if (!view.isPage || page === null) return [];
  const words: string[] = [];
  if (!page.loaded) words.push('not loaded');
  if (page.crashed) words.push('crashed');
  if (page.paused) words.push('paused');
  if (page.loading) words.push('loading');
  if (page.muted) words.push('muted');
  else if (page.audible) words.push('playing audio');
  if (page.camera) words.push('camera');
  if (page.microphone) words.push('microphone');
  if (page.screen) words.push('sharing screen');
  if (page.devtools) words.push('DevTools open');
  return words;
}

export function formatBytes(bytes: number): string {
  if (bytes < 0) return '';
  if (bytes < 1024) return `${bytes} B`;
  const units = ['KB', 'MB', 'GB', 'TB'];
  let value = bytes / 1024;
  let unit = 0;
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024;
    unit++;
  }
  return `${value >= 100 ? value.toFixed(0) : value.toFixed(1)} ${units[unit] ?? 'TB'}`;
}

export function formatCpu(cpu: number): string {
  return `${cpu.toFixed(1)} %`;
}

/** The value a column sorts by; null sorts last. */
export function sortValue(view: ViewRow, column: ColumnId): number | string | null {
  const { row, page } = view;
  const loaded = row.process !== null;
  switch (column) {
    case 'task':
      return rowTitle(view).toLowerCase();
    case 'profile':
      return page?.profile.toLowerCase() ?? null;
    case 'memory':
      return loaded && row.memory >= 0 ? row.memory : null;
    case 'cpu':
      return loaded ? row.cpu : null;
    case 'status':
      return statusWords(view).join(' ') || null;
    case 'pid':
      return row.pid > 0 ? row.pid : null;
    case 'gpu':
      return loaded && row.gpuMemory >= 0 ? row.gpuMemory : null;
    case 'js_memory':
      return view.isPage && page !== null && page.jsUsed >= 0 ? page.jsUsed : null;
    case 'network':
      return view.isPage && page !== null && page.network >= 0 ? page.network : null;
    case 'address':
      return page?.url.toLowerCase() ?? null;
    case 'type':
      return typeName(view).toLowerCase();
    case 'shared':
      return view.isPage ? view.shared : null;
    case 'window':
      return page?.window.toLowerCase() ?? null;
  }
}

/** The cell's text (empty when the value belongs to the process's first row). */
export function cellText(view: ViewRow, column: ColumnId): string {
  const { row, page } = view;
  const loaded = row.process !== null;
  switch (column) {
    case 'task':
      return rowTitle(view);
    case 'profile':
      return view.isPage ? (page?.profile ?? '') : '';
    case 'memory':
      return loaded && view.first ? formatBytes(row.memory) : '';
    case 'cpu':
      return loaded && view.first ? formatCpu(row.cpu) : '';
    case 'status':
      return statusWords(view).join(', ');
    case 'pid':
      return row.pid > 0 && view.first ? String(row.pid) : '';
    case 'gpu':
      if (!loaded || !view.first || row.gpuMemory < 0) return '';
      return row.gpuInflated ? `${formatBytes(row.gpuMemory)} (all pages)` : formatBytes(row.gpuMemory);
    case 'js_memory':
      if (!view.isPage || page === null || page.jsUsed < 0) return '';
      return `${formatBytes(page.jsUsed)} / ${formatBytes(page.jsTotal)}`;
    case 'network':
      if (!view.isPage || page === null || page.brouk || !page.loaded) return '';
      return page.network < 0 ? '' : `${formatBytes(Math.round(page.network))}/s`;
    case 'address':
      return view.isPage ? (page?.url ?? '') : '';
    case 'type':
      return typeName(view);
    case 'shared':
      return view.isPage && view.shared > 0 ? String(view.shared) : '';
    case 'window':
      return view.isPage ? (page?.window ?? '') : '';
  }
}

// ---- Rows ----

/** Lowercase without accents, as the palette and help search fold. */
export function fold(text: string): string {
  return text.normalize('NFD').replace(/[̀-ͯ]/g, '').toLowerCase();
}

function matches(view: ViewRow, words: readonly string[]): boolean {
  if (words.length === 0) return true;
  const page = view.page;
  const text = fold([rowTitle(view), page?.url ?? '', page?.profile ?? ''].join(' '));
  return words.every((word) => text.includes(word));
}

function compare(a: number | string | null, b: number | string | null, descending: boolean): number {
  if (a === null || b === null) return a === b ? 0 : a === null ? 1 : -1;
  const order = typeof a === 'number' && typeof b === 'number' ? a - b : String(a).localeCompare(String(b));
  return descending ? -order : order;
}

/** A group's value for sorting: the process's value, or the sum of per-page values. */
function groupValue(views: readonly ViewRow[], column: ColumnId, descending: boolean): number | string | null {
  const values = views.map((view) => sortValue(view, column));
  if (column === 'js_memory' || column === 'network') {
    const numbers = values.filter((value): value is number => typeof value === 'number');
    return numbers.length === 0 ? null : numbers.reduce((sum, value) => sum + value, 0);
  }
  // The best value of the group (the first after sorting its rows).
  const sorted = [...values].sort((a, b) => compare(a, b, descending));
  return sorted[0] ?? null;
}

/**
 * The rows to show: grouped by process, groups ordered by `sort`, rows
 * inside a group too (pages first). Only rows matching `query` are shown;
 * a group keeps its matching rows.
 */
export function viewRows(list: TaskList, sort: Sort, query: string): ViewRow[] {
  const groups = new Map<string, TaskRow[]>();
  for (const row of list.rows) {
    const group = row.process === null ? `page-${row.key}` : `process-${row.process}`;
    const members = groups.get(group);
    if (members === undefined) groups.set(group, [row]);
    else members.push(row);
  }
  const words = fold(query)
    .split(/\s+/)
    .filter((word) => word !== '');
  const result: ViewRow[][] = [];
  for (const members of groups.values()) {
    const tabs = members.filter((row) => row.type === 'tab' || row.type === 'brouk');
    let views: ViewRow[] = members.map((row) => {
      const page = row.page === null ? null : (list.pages.get(row.page) ?? null);
      const isPage = (row.type === 'tab' || row.type === 'brouk') && page !== null;
      return {
        row,
        page,
        isPage,
        groupSize: members.length,
        first: false,
        shared: isPage ? tabs.length - 1 : 0,
      };
    });
    views.sort((a, b) => {
      if (a.isPage !== b.isPage) return a.isPage ? -1 : 1;
      return compare(sortValue(a, sort.column), sortValue(b, sort.column), sort.descending);
    });
    views = views.map((view, index) => ({ ...view, first: index === 0 }));
    const shown = views.filter((view) => matches(view, words));
    if (shown.length > 0) result.push(shown);
  }
  result.sort((a, b) =>
    compare(groupValue(a, sort.column, sort.descending), groupValue(b, sort.column, sort.descending), sort.descending),
  );
  return result.flat();
}

/** Pages of the process of each row given, that are not among them (End process asks first). */
export function otherPagesInProcesses(list: TaskList, keys: ReadonlySet<string>): string[] {
  const processes = new Set<number>();
  for (const row of list.rows) {
    if (keys.has(row.key) && row.process !== null) processes.add(row.process);
  }
  const titles: string[] = [];
  for (const row of list.rows) {
    if (row.process === null || !processes.has(row.process) || keys.has(row.key)) continue;
    if (row.type !== 'tab' && row.type !== 'brouk') continue;
    const page = row.page === null ? undefined : list.pages.get(row.page);
    if (page !== undefined) titles.push(page.title === '' ? page.url : page.title);
  }
  return titles;
}

// ---- Copy ----

function csvField(text: string): string {
  return /[",\n\r]/.test(text) ? `"${text.replace(/"/g, '""')}"` : text;
}

/** The rows as CSV with a header line; values as shown, process values on each row. */
export function toCsv(views: readonly ViewRow[], columns: readonly ColumnId[], time: number): string {
  const lines = [[...columns.map((id) => columnById(id).label), 'Copied at'].map(csvField).join(',')];
  const at = new Date(time).toISOString();
  for (const view of views) {
    const full = { ...view, first: true };
    lines.push([...columns.map((id) => cellText(full, id)), at].map(csvField).join(','));
  }
  return `${lines.join('\n')}\n`;
}

export interface Sample {
  /** Milliseconds since 1970. */
  readonly time: number;
  readonly cpu: number;
  readonly memory: number;
}

/** The rows as JSON: the shown values, raw numbers, and graph samples. */
export function toJson(
  views: readonly ViewRow[],
  columns: readonly ColumnId[],
  time: number,
  samples: (key: string) => readonly Sample[],
): string {
  const rows = views.map((view) => {
    const full = { ...view, first: true };
    const values: Record<string, string> = {};
    for (const id of columns) values[id] = cellText(full, id);
    return {
      values,
      key: view.row.key,
      type: view.row.type,
      processId: view.row.pid > 0 ? view.row.pid : null,
      cpuPercent: view.row.process === null ? null : view.row.cpu,
      memoryBytes: view.row.process === null || view.row.memory < 0 ? null : view.row.memory,
      samples: samples(view.row.key).map((sample) => ({
        time: new Date(sample.time).toISOString(),
        cpuPercent: sample.cpu,
        memoryBytes: sample.memory,
      })),
    };
  });
  return `${JSON.stringify({ copiedAt: new Date(time).toISOString(), columns, rows }, null, 2)}\n`;
}

// ---- History ----

/**
 * CPU and memory of each row over time, for the graph. Rows that share a
 * process have the process's values, so a group is graphed as its total.
 */
export class History {
  private readonly series = new Map<string, Sample[]>();

  constructor(private lengthMs: number) {}

  setLength(lengthMs: number): void {
    this.lengthMs = lengthMs;
  }

  /** Adds one refresh; rows that ended lose their history. */
  add(list: TaskList): void {
    const alive = new Set<string>();
    for (const row of list.rows) {
      alive.add(row.key);
      if (row.process === null || row.memory < 0) continue;
      let samples = this.series.get(row.key);
      if (samples === undefined) {
        samples = [];
        this.series.set(row.key, samples);
      }
      samples.push({ time: list.time, cpu: row.cpu, memory: row.memory });
    }
    for (const key of [...this.series.keys()]) {
      if (!alive.has(key)) this.series.delete(key);
    }
    this.prune(list.time);
  }

  private prune(now: number): void {
    for (const samples of this.series.values()) {
      const start = samples.findIndex((sample) => sample.time >= now - this.lengthMs);
      if (start > 0) samples.splice(0, start);
      else if (start < 0) samples.length = 0;
    }
  }

  samples(key: string): readonly Sample[] {
    return this.series.get(key) ?? [];
  }
}

// ---- Graph geometry ----

/** A "nice" maximum for an axis: 1, 2 or 5 times a power of ten, at least `value`. */
export function niceMax(value: number): number {
  if (value <= 0) return 1;
  const power = 10 ** Math.floor(Math.log10(value));
  for (const step of [1, 2, 5, 10]) {
    if (step * power >= value) return step * power;
  }
  return 10 * power;
}

/** A "nice" maximum for a memory axis, in whole KB, MB or GB steps. */
export function niceBytes(value: number): number {
  for (const unit of [1024 ** 3, 1024 ** 2, 1024]) {
    if (value >= unit) return niceMax(value / unit) * unit;
  }
  return niceMax(value);
}

/** The sample nearest to `time`, or null. */
export function nearestSample(samples: readonly Sample[], time: number): Sample | null {
  let best: Sample | null = null;
  for (const sample of samples) {
    if (best === null || Math.abs(sample.time - time) < Math.abs(best.time - time)) best = sample;
  }
  return best;
}
