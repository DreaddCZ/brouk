import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';

// viewer.js is a plain script: run it without a document and take its
// functions from the context.
const context: Record<string, unknown> = {};
runInNewContext(
  readFileSync(new URL('./viewer.js', import.meta.url), 'utf8') +
    '\nthis.api = { jsonViewIsJsonDocument, jsonViewPath, jsonViewSummary, jsonViewSearch, jsonViewIsLink };',
  context,
);
const api = context['api'] as {
  jsonViewIsJsonDocument(type: string, text: string): boolean;
  jsonViewPath(parts: (string | number)[]): string;
  jsonViewSummary(value: unknown): string;
  jsonViewSearch(value: unknown, query: string, limit: number): (string | number)[][];
  jsonViewIsLink(text: string): boolean;
};

test('detects JSON documents', () => {
  assert.equal(api.jsonViewIsJsonDocument('application/json', ''), true);
  assert.equal(api.jsonViewIsJsonDocument('application/problem+json; charset=utf-8', ''), true);
  assert.equal(api.jsonViewIsJsonDocument('text/plain', '  {"a":1}'), true);
  assert.equal(api.jsonViewIsJsonDocument('text/plain', 'hello'), false);
  assert.equal(api.jsonViewIsJsonDocument('text/html', '{"a":1}'), false);
});

test('paths', () => {
  assert.equal(api.jsonViewPath([]), '$');
  assert.equal(api.jsonViewPath(['users', 0, 'name']), '$.users[0].name');
  assert.equal(api.jsonViewPath(['a b', 'x-y', '_ok']), '$["a b"]["x-y"]._ok');
});

test('summaries', () => {
  assert.equal(api.jsonViewSummary([1, 2]), '[2]');
  assert.equal(api.jsonViewSummary({ a: 1 }), '{1}');
  assert.equal(api.jsonViewSummary('x'), '"x"');
  assert.equal(api.jsonViewSummary(null), 'null');
});

test('search finds keys and values in order, with a limit', () => {
  const data = { name: 'Brouk', items: [{ id: 1, label: 'beetle' }, { id: 2, label: 'Bee' }] };
  assert.deepEqual(JSON.parse(JSON.stringify(api.jsonViewSearch(data, 'bee', 10))), [
    ['items', 0, 'label'],
    ['items', 1, 'label'],
  ]);
  assert.deepEqual(JSON.parse(JSON.stringify(api.jsonViewSearch(data, 'items', 10))), [['items']]);
  assert.equal(api.jsonViewSearch(data, 'e', 2).length, 2);
  assert.equal(api.jsonViewSearch(data, '', 10).length, 0);
});

test('search in a large document is quick', () => {
  const big = Array.from({ length: 200000 }, (_, i) => ({ id: i, value: `item ${i}` }));
  const started = Date.now();
  const found = api.jsonViewSearch(big, 'item 199999', 10);
  assert.equal(found.length, 1);
  assert.ok(Date.now() - started < 2000);
});

test('only http(s) strings become links', () => {
  assert.equal(api.jsonViewIsLink('https://example.com/a?b=1'), true);
  assert.equal(api.jsonViewIsLink('javascript:alert(1)'), false);
  assert.equal(api.jsonViewIsLink('https://a b'), false);
});
