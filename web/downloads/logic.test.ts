import { test } from 'node:test';
import assert from 'node:assert/strict';
import { actionsFor, dayLabel, searchDownloads, statusOf } from './logic.js';
import { parseDownload, type Download } from './api.js';

function download(extra: Partial<Download>): Download {
  return { profile: 'p', id: 1, url: 'https://example.com/a.zip', name: 'a.zip', site: 'example.com', state: 'complete',
    status: 'Done, 1 MB', received: 1, total: 1, start: 0, exists: true, ...extra };
}

test('search by file name and URL', () => {
  const all = [download({ id: 1, name: 'Report.pdf' }), download({ id: 2, url: 'https://files.corp/x', name: 'x' })];
  assert.deepEqual(searchDownloads(all, 'report').map((d) => d.id), [1]);
  assert.deepEqual(searchDownloads(all, 'corp').map((d) => d.id), [2]);
});

test('deleted files', () => {
  assert.equal(statusOf(download({ exists: false })), 'Deleted');
  assert.deepEqual(actionsFor(download({ exists: false })), ['retry', 'copy', 'remove']);
  assert.deepEqual(actionsFor(download({})), ['open', 'reveal', 'copy', 'remove', 'delete']);
  assert.deepEqual(actionsFor(download({ state: 'in_progress' })).slice(0, 2), ['pause', 'cancel']);
});

test('day labels', () => {
  const now = new Date(2026, 9, 25, 12);
  assert.equal(dayLabel(new Date(2026, 9, 25, 8).getTime() / 1000, now), 'Today');
  assert.equal(dayLabel(new Date(2026, 9, 24, 23).getTime() / 1000, now), 'Yesterday');
});

test('answers are checked', () => {
  assert.throws(() => parseDownload({ ...download({}), state: 'weird' }));
  assert.equal(parseDownload(download({})).name, 'a.zip');
});
