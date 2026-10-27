/**
 * The task manager page's messages to the browser (020). Every answer is
 * checked before the page uses it.
 */
import { BridgeError, isRecord, send, type Transport } from '../shared/bridge.js';

export type RowType = 'tab' | 'brouk' | 'subframe' | 'worker' | 'browser' | 'gpu' | 'utility' | 'other';

/** One task, or a page that is not loaded. */
export interface TaskRow {
  /** Stable while the row exists: the page's key for pages, else "task-<id>". */
  readonly key: string;
  readonly task: number | null;
  readonly type: RowType;
  /** Chromium's title (for rows that are not pages). */
  readonly title: string;
  /** The page it is or belongs to. */
  readonly page: string | null;
  /** Rows of one process share it; null when not loaded. */
  readonly process: number | null;
  readonly pid: number;
  /** Of the process: % of one core, bytes (-1: not measured). */
  readonly cpu: number;
  readonly memory: number;
  readonly gpuMemory: number;
  readonly gpuInflated: boolean;
  readonly killable: boolean;
}

export interface PageInfo {
  readonly title: string;
  readonly url: string;
  readonly icon: string;
  readonly profile: string;
  readonly color: string;
  readonly window: string;
  readonly loaded: boolean;
  readonly brouk: boolean;
  readonly ownWindow: boolean;
  readonly audible: boolean;
  readonly muted: boolean;
  readonly camera: boolean;
  readonly microphone: boolean;
  readonly screen: boolean;
  readonly paused: boolean;
  readonly loading: boolean;
  readonly crashed: boolean;
  readonly devtools: boolean;
  /** The tab shown in its window (it cannot be unloaded). */
  readonly shown: boolean;
  /** Bytes per second; -1 when not known yet. */
  readonly network: number;
  readonly jsUsed: number;
  readonly jsTotal: number;
}

export interface TaskList {
  /** Milliseconds since 1970. */
  readonly time: number;
  readonly processors: number;
  readonly rows: readonly TaskRow[];
  readonly pages: ReadonlyMap<string, PageInfo>;
  readonly totalMemory: number;
  readonly totalCpu: number;
}

export interface TaskSettings {
  readonly columns: string;
  readonly sort: string;
  /** Seconds. */
  readonly refresh: number;
  /** Minutes. */
  readonly history: number;
  readonly graph: boolean;
}

export type PageAction = 'goto' | 'pause' | 'resume' | 'unload' | 'mute' | 'unmute' | 'reload' | 'devtools';

const kRowTypes: readonly RowType[] = ['tab', 'brouk', 'subframe', 'worker', 'browser', 'gpu', 'utility', 'other'];

function invalid(): BridgeError {
  return new BridgeError('The browser sent an answer that could not be read.');
}

function text(record: Record<string, unknown>, key: string): string {
  const value = record[key];
  if (typeof value !== 'string') throw invalid();
  return value;
}

function num(record: Record<string, unknown>, key: string): number {
  const value = record[key];
  if (typeof value !== 'number' || !Number.isFinite(value)) throw invalid();
  return value;
}

function flag(record: Record<string, unknown>, key: string): boolean {
  const value = record[key];
  if (typeof value !== 'boolean') throw invalid();
  return value;
}

function numOrNull(record: Record<string, unknown>, key: string): number | null {
  return record[key] === null ? null : num(record, key);
}

function textOrNull(record: Record<string, unknown>, key: string): string | null {
  return record[key] === null ? null : text(record, key);
}

export function parseRow(value: unknown): TaskRow {
  if (!isRecord(value)) throw invalid();
  const type = text(value, 'type');
  if (!(kRowTypes as readonly string[]).includes(type)) throw invalid();
  return {
    key: text(value, 'key'),
    task: numOrNull(value, 'task'),
    type: type as RowType,
    title: text(value, 'title'),
    page: textOrNull(value, 'page'),
    process: numOrNull(value, 'process'),
    pid: num(value, 'pid'),
    cpu: num(value, 'cpu'),
    memory: num(value, 'memory'),
    gpuMemory: num(value, 'gpuMemory'),
    gpuInflated: flag(value, 'gpuInflated'),
    killable: flag(value, 'killable'),
  };
}

export function parsePage(value: unknown): PageInfo {
  if (!isRecord(value)) throw invalid();
  const icon = text(value, 'icon');
  return {
    title: text(value, 'title'),
    url: text(value, 'url'),
    // Only images the browser made.
    icon: icon.startsWith('data:image/png;base64,') ? icon : '',
    profile: text(value, 'profile'),
    color: text(value, 'color'),
    window: text(value, 'window'),
    loaded: flag(value, 'loaded'),
    brouk: flag(value, 'brouk'),
    ownWindow: flag(value, 'ownWindow'),
    audible: flag(value, 'audible'),
    muted: flag(value, 'muted'),
    camera: flag(value, 'camera'),
    microphone: flag(value, 'microphone'),
    screen: flag(value, 'screen'),
    paused: flag(value, 'paused'),
    loading: flag(value, 'loading'),
    crashed: flag(value, 'crashed'),
    devtools: flag(value, 'devtools'),
    shown: flag(value, 'shown'),
    network: num(value, 'network'),
    jsUsed: num(value, 'jsUsed'),
    jsTotal: num(value, 'jsTotal'),
  };
}

export function parseList(value: unknown): TaskList {
  if (!isRecord(value)) throw invalid();
  const rows = value['rows'];
  const pages = value['pages'];
  if (!Array.isArray(rows) || !isRecord(pages)) throw invalid();
  const pageMap = new Map<string, PageInfo>();
  for (const [key, page] of Object.entries(pages)) pageMap.set(key, parsePage(page));
  return {
    time: num(value, 'time'),
    processors: num(value, 'processors'),
    rows: (rows as unknown[]).map(parseRow),
    pages: pageMap,
    totalMemory: num(value, 'totalMemory'),
    totalCpu: num(value, 'totalCpu'),
  };
}

export function parseSettings(value: unknown): TaskSettings {
  if (!isRecord(value)) throw invalid();
  return {
    columns: text(value, 'columns'),
    sort: text(value, 'sort'),
    refresh: num(value, 'refresh'),
    history: num(value, 'history'),
    graph: flag(value, 'graph'),
  };
}

export async function loadList(transport: Transport | null, jsMemory: boolean): Promise<TaskList> {
  return parseList(await send('tasks.list', { jsMemory }, transport));
}

export async function loadSettings(transport: Transport | null): Promise<TaskSettings> {
  return parseSettings(await send('tasks.settings', {}, transport));
}

export async function act(transport: Transport | null, action: PageAction, pages: readonly string[]): Promise<void> {
  await send('tasks.action', { action, pages }, transport);
}

export async function endTasks(transport: Transport | null, tasks: readonly number[]): Promise<void> {
  await send('tasks.end', { tasks }, transport);
}

export async function saveSetting(
  transport: Transport | null,
  key: 'columns' | 'sort' | 'graph',
  value: string | boolean,
): Promise<void> {
  await send('tasks.set', { key, value }, transport);
}

export async function copyText(transport: Transport | null, text: string): Promise<void> {
  await send('tasks.copy', { text }, transport);
}
