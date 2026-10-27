import { test } from 'node:test';
import assert from 'node:assert/strict';
import { parseList, parseSettings, type PageInfo, type TaskList, type TaskRow } from './api.js';
import {
  cellText,
  formatBytes,
  formatSort,
  History,
  nearestSample,
  nextSort,
  niceBytes,
  niceMax,
  otherPagesInProcesses,
  parseColumns,
  parseSort,
  statusWords,
  toCsv,
  toJson,
  viewRows,
} from './logic.js';

function row(extra: Partial<TaskRow>): TaskRow {
  return {
    key: 'task-1', task: 1, type: 'other', title: '', page: null, process: 0, pid: 0, cpu: 0,
    memory: 1000, gpuMemory: -1, gpuInflated: false, killable: true, ...extra,
  };
}

function page(extra: Partial<PageInfo>): PageInfo {
  return {
    title: 'Page', url: 'https://example.com/', icon: '', profile: 'Work', color: 'blue', window: 'Window 1',
    loaded: true, brouk: false, ownWindow: false, audible: false, muted: false, camera: false, microphone: false,
    screen: false, paused: false, loading: false, crashed: false, devtools: false, shown: false, network: -1,
    jsUsed: -1, jsTotal: -1, ...extra,
  };
}

function list(rows: TaskRow[], pages: Record<string, PageInfo>, time = 1_000_000): TaskList {
  return { time, processors: 8, rows, pages: new Map(Object.entries(pages)), totalMemory: 0, totalCpu: 0 };
}

// Browser process, two tabs sharing a renderer with a worker, a tab of its own, a tab not loaded.
function sample(): TaskList {
  return list(
    [
      row({ key: 'task-1', task: 1, type: 'browser', title: 'Browser', process: 0, memory: 500, cpu: 2, killable: false }),
      row({ key: 'tab-1', task: 10, type: 'tab', page: 'tab-1', process: 1, memory: 300, cpu: 1, pid: 41 }),
      row({ key: 'tab-2', task: 11, type: 'tab', page: 'tab-2', process: 1, memory: 300, cpu: 1, pid: 41 }),
      row({ key: 'task-12', task: 12, type: 'worker', title: 'Service Worker: a', page: 'tab-1', process: 1, memory: 300, cpu: 1, pid: 41 }),
      row({ key: 'tab-3', task: 20, type: 'tab', page: 'tab-3', process: 2, memory: 900, cpu: 50, pid: 42 }),
      row({ key: 'tab-4', task: null, type: 'tab', page: 'tab-4', process: null, memory: -1, cpu: 0 }),
    ],
    {
      'tab-1': page({ title: 'Mail', url: 'https://mail.example/' }),
      'tab-2': page({ title: 'Calendar', url: 'https://cal.example/', profile: 'Personal' }),
      'tab-3': page({ title: 'Busy loop', url: 'https://busy.example/', audible: true }),
      'tab-4': page({ title: 'Later', url: 'https://later.example/', loaded: false }),
    },
  );
}

test('columns from the setting: known ones once, task first', () => {
  assert.deepEqual(parseColumns('memory, cpu, memory, nonsense, PID'), ['task', 'memory', 'cpu', 'pid']);
  assert.deepEqual(parseColumns(''), ['task', 'profile', 'memory', 'cpu', 'status', 'pid']);
  assert.deepEqual(parseColumns('task'), ['task', 'profile', 'memory', 'cpu', 'status', 'pid']);
});

test('sort order from the setting and header clicks', () => {
  assert.deepEqual(parseSort('cpu, ascending'), { column: 'cpu', descending: false });
  assert.deepEqual(parseSort('task'), { column: 'task', descending: false });
  assert.deepEqual(parseSort('nonsense, descending'), { column: 'memory', descending: true });
  assert.equal(formatSort({ column: 'cpu', descending: true }), 'cpu, descending');
  assert.deepEqual(nextSort({ column: 'cpu', descending: true }, 'cpu'), { column: 'cpu', descending: false });
  assert.deepEqual(nextSort({ column: 'cpu', descending: true }, 'task'), { column: 'task', descending: false });
  assert.deepEqual(nextSort({ column: 'task', descending: false }, 'memory'), { column: 'memory', descending: true });
});

test('process groups stay together and sort by the process value', () => {
  const views = viewRows(sample(), { column: 'cpu', descending: true }, '');
  assert.deepEqual(
    views.map((v) => v.row.key),
    ['tab-3', 'task-1', 'tab-1', 'tab-2', 'task-12', 'tab-4'],
  );
  // Pages first inside a group; process values only on its first row.
  const mail = views.find((v) => v.row.key === 'tab-1')!;
  const calendar = views.find((v) => v.row.key === 'tab-2')!;
  assert.equal(mail.first, true);
  assert.equal(cellText(mail, 'memory'), '300 B');
  assert.equal(cellText(calendar, 'memory'), '');
  assert.equal(cellText(mail, 'shared'), '1');
  assert.equal(cellText(calendar, 'pid'), '');
  // A page not loaded sorts last and shows no values.
  const later = views.at(-1)!;
  assert.equal(cellText(later, 'cpu'), '');
  assert.deepEqual(statusWords(later), ['not loaded']);
});

test('busy tab sorts to the top by CPU', () => {
  const views = viewRows(sample(), { column: 'cpu', descending: true }, '');
  assert.equal(views[0]?.row.key, 'tab-3');
  assert.equal(cellText(views[0]!, 'cpu'), '50.0 %');
});

test('filter by title, address or profile, ignoring case and accents', () => {
  const data = sample();
  // The page's worker comes along.
  assert.deepEqual(viewRows(data, { column: 'memory', descending: true }, 'MAIL').map((v) => v.row.key), [
    'tab-1',
    'task-12',
  ]);
  assert.deepEqual(viewRows(data, { column: 'memory', descending: true }, 'personal').map((v) => v.row.key), ['tab-2']);
  assert.deepEqual(viewRows(data, { column: 'memory', descending: true }, 'cal.example').map((v) => v.row.key), ['tab-2']);
  const accents = list([row({ key: 'tab-9', type: 'tab', page: 'tab-9' })], { 'tab-9': page({ title: 'Nastavení' }) });
  assert.equal(viewRows(accents, { column: 'memory', descending: true }, 'nastaveni').length, 1);
});

test('names of rows that are not pages', () => {
  const views = viewRows(sample(), { column: 'memory', descending: true }, '');
  const browser = views.find((v) => v.row.type === 'browser');
  const worker = views.find((v) => v.row.type === 'worker');
  assert.equal(cellText(browser!, 'task'), 'Brouk');
  assert.equal(cellText(worker!, 'task'), 'Service Worker: a');
  assert.equal(cellText(worker!, 'type'), 'Worker');
  // A worker shows no page columns of its own.
  assert.equal(cellText(worker!, 'profile'), '');
});

test('status words', () => {
  const views = viewRows(sample(), { column: 'memory', descending: true }, '');
  const busy = views.find((v) => v.row.key === 'tab-3');
  assert.deepEqual(statusWords(busy!), ['playing audio']);
  const muted = list([row({ key: 'tab-5', type: 'tab', page: 'tab-5' })], {
    'tab-5': page({ audible: true, muted: true, paused: true, camera: true, microphone: true, devtools: true }),
  });
  assert.deepEqual(statusWords(viewRows(muted, { column: 'cpu', descending: true }, '')[0]!), [
    'paused',
    'muted',
    'camera',
    'microphone',
    'DevTools open',
  ]);
});

test('ending a shared process names the other tabs', () => {
  assert.deepEqual(otherPagesInProcesses(sample(), new Set(['tab-1'])), ['Calendar']);
  assert.deepEqual(otherPagesInProcesses(sample(), new Set(['tab-1', 'tab-2'])), []);
  assert.deepEqual(otherPagesInProcesses(sample(), new Set(['tab-3'])), []);
});

test('bytes', () => {
  assert.equal(formatBytes(-1), '');
  assert.equal(formatBytes(512), '512 B');
  assert.equal(formatBytes(1536), '1.5 KB');
  assert.equal(formatBytes(300 * 1024 * 1024), '300 MB');
});

test('copy as CSV: valid fields, process values on every row', () => {
  const views = viewRows(sample(), { column: 'memory', descending: true }, 'example');
  const csv = toCsv(views, ['task', 'memory'], Date.UTC(2026, 9, 27, 12, 0, 0));
  const lines = csv.trim().split('\n');
  assert.equal(lines[0], 'Task,Memory,Copied at');
  assert.equal(lines.length, views.length + 1);
  assert.ok(lines.includes('Calendar,300 B,2026-10-27T12:00:00.000Z'));
  const quoted = list([row({ key: 'tab-7', type: 'tab', page: 'tab-7' })], { 'tab-7': page({ title: 'A, "B"' }) });
  assert.ok(/^Task,Copied at\n"A, ""B""",/.test(toCsv(viewRows(quoted, { column: 'cpu', descending: true }, ''), ['task'], 0)));
});

test('copy as JSON includes the graph samples', () => {
  const views = viewRows(sample(), { column: 'memory', descending: true }, 'busy');
  const json = JSON.parse(
    toJson(views, ['task', 'cpu'], 0, (key) => (key === 'tab-3' ? [{ time: 0, cpu: 5, memory: 10 }] : [])),
  ) as { columns: string[]; rows: Array<{ values: Record<string, string>; samples: unknown[]; cpuPercent: number }> };
  assert.deepEqual(json.columns, ['task', 'cpu']);
  assert.equal(json.rows[0]?.values['task'], 'Busy loop');
  assert.equal(json.rows[0]?.cpuPercent, 50);
  assert.deepEqual(json.rows[0]?.samples, [{ time: '1970-01-01T00:00:00.000Z', cpuPercent: 5, memoryBytes: 10 }]);
});

test('history keeps the configured length and forgets ended rows', () => {
  const history = new History(3000);
  for (let t = 0; t <= 5000; t += 1000) {
    history.add(list([row({ key: 'tab-1', type: 'tab', page: 'tab-1', cpu: t / 1000, memory: t })], {}, t));
  }
  assert.deepEqual(
    history.samples('tab-1').map((s) => s.time),
    [2000, 3000, 4000, 5000],
  );
  history.add(list([], {}, 6000));
  assert.deepEqual(history.samples('tab-1'), []);
  // Rows that are not loaded have no samples.
  history.add(list([row({ key: 'tab-2', process: null, memory: -1 })], {}, 7000));
  assert.deepEqual(history.samples('tab-2'), []);
});

test('graph helpers', () => {
  assert.equal(niceMax(0), 1);
  assert.equal(niceMax(7), 10);
  assert.equal(niceMax(130), 200);
  assert.equal(niceMax(450), 500);
  assert.equal(niceBytes(477 * 1024 * 1024), 500 * 1024 * 1024);
  assert.equal(niceBytes(1.3 * 1024 ** 3), 2 * 1024 ** 3);
  assert.equal(niceBytes(300), 500);
  const samples = [{ time: 0, cpu: 1, memory: 1 }, { time: 1000, cpu: 2, memory: 2 }];
  assert.equal(nearestSample(samples, 700)?.time, 1000);
  assert.equal(nearestSample([], 700), null);
});

test('answers from the browser are checked', () => {
  assert.throws(() => parseList({ rows: [{ key: 'x' }], pages: {} }));
  assert.throws(() => parseSettings({ columns: 1 }));
  const parsed = parseList({
    time: 1,
    processors: 8,
    totalMemory: 10,
    totalCpu: 1,
    rows: [{ key: 'tab-1', task: 5, type: 'tab', title: '', page: 'tab-1', process: 0, pid: 3, cpu: 1, memory: 2,
      gpuMemory: -1, gpuInflated: false, killable: true }],
    pages: { 'tab-1': { ...page({}), icon: 'javascript:alert(1)' } },
  });
  assert.equal(parsed.rows[0]?.pid, 3);
  // Only images the browser made are shown.
  assert.equal(parsed.pages.get('tab-1')?.icon, '');
  assert.throws(() => parseList({ time: 1, processors: 1, totalMemory: 0, totalCpu: 0, pages: {},
    rows: [{ key: 'a', task: 1, type: 'evil', title: '', page: null, process: 0, pid: 0, cpu: 0, memory: 0, gpuMemory: 0,
      gpuInflated: false, killable: false }] }));
});
