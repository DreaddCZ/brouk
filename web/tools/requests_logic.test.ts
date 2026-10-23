import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  exportFileNames, matchesFilters, parseDetails, parseSecretHeaders, parseSummary, requestName, toPostman,
  variableName, type RequestDetails,
} from './requests_logic.js';

function request(overrides: Partial<RequestDetails> = {}): RequestDetails {
  return {
    seq: 1, id: '1', url: 'https://api.example.com/v1/users?page=2', method: 'GET', type: 'Fetch', status: 200,
    mimeType: 'application/json', finished: true, error: '', started: 0, duration: 0.1, size: 10,
    requestHeaders: [['Accept', 'application/json'], ['Authorization', 'Bearer secret-1'], [':authority', 'x']],
    responseHeaders: [],
    ...overrides,
  };
}

test('parsing', () => {
  assert.equal(parseSummary({ url: 'x' }), null);
  assert.equal(parseSummary({ seq: 3, url: 'https://a/' })?.duration, -1);
  const details = parseDetails({ seq: 1, url: 'https://a/', requestHeaders: [['A', 'b'], ['bad']], postData: 'x' });
  assert.deepEqual(details?.requestHeaders, [['A', 'b']]);
  assert.equal(details?.postData, 'x');
});

test('filters', () => {
  const base = { type: 'fetch', method: '', status: 'all', text: '' } as const;
  assert.equal(matchesFilters(request(), base), true);
  assert.equal(matchesFilters(request({ type: 'Image' }), base), false);
  assert.equal(matchesFilters(request({ type: 'Image' }), { ...base, type: 'all' }), true);
  assert.equal(matchesFilters(request({ type: 'Ping' }), { ...base, type: 'other' }), true);
  assert.equal(matchesFilters(request(), { ...base, method: 'POST' }), false);
  assert.equal(matchesFilters(request({ status: 404 }), { ...base, status: '4xx' }), true);
  assert.equal(matchesFilters(request({ status: 404 }), { ...base, status: 'failed' }), true);
  assert.equal(matchesFilters(request({ status: 0, error: 'net::ERR' }), { ...base, status: 'failed' }), true);
  assert.equal(matchesFilters(request(), { ...base, status: '5xx' }), false);
  assert.equal(matchesFilters(request(), { ...base, text: 'USERS' }), true);
  assert.equal(matchesFilters(request(), { ...base, text: 'orders' }), false);
});

test('names', () => {
  assert.equal(requestName('POST', 'https://a.b/x/y?z=1'), 'POST /x/y');
  assert.equal(variableName('api.example.com:8443'), 'api_example_com_8443');
  assert.deepEqual(parseSecretHeaders(' Authorization, Cookie ,, x-api-key'), ['authorization', 'cookie', 'x-api-key']);
  assert.deepEqual(exportFileNames('My: API'), {
    collection: 'My- API.postman_collection.json', environment: 'My- API.postman_environment.json',
  });
});

test('Postman export keeps requests and moves secrets to the environment', () => {
  const requests = [
    request(),
    request({
      seq: 2, method: 'POST', url: 'https://api.example.com/v1/users',
      requestHeaders: [['Content-Type', 'application/json'], ['Authorization', 'Bearer secret-2'], ['Content-Length', '9']],
      postData: '{"a":1}',
    }),
    request({
      seq: 3, method: 'POST', url: 'https://login.example.com/token',
      requestHeaders: [['Content-Type', 'application/x-www-form-urlencoded'], ['Cookie', 's=1']],
      postData: 'grant_type=password&user=a',
    }),
  ];
  const { collection, environment } = toPostman(requests, 'Example', ['authorization', 'cookie']);
  const text = JSON.stringify(collection);
  assert.ok(!text.includes('secret-1') && !text.includes('secret-2') && !text.includes('s=1'));
  const info = collection['info'] as Record<string, unknown>;
  assert.equal(info['schema'], 'https://schema.getpostman.com/json/collection/v2.1.0/collection.json');
  const folders = collection['item'] as { name: string; item: { name: string; request: Record<string, unknown> }[] }[];
  assert.deepEqual(folders.map((f) => f.name), ['api.example.com', 'login.example.com']);
  const first = folders[0]?.item[0]?.request as Record<string, unknown>;
  assert.deepEqual(first['url'], {
    raw: '{{baseUrl_api_example_com}}/v1/users?page=2', host: ['{{baseUrl_api_example_com}}'],
    path: ['v1', 'users'], query: [{ key: 'page', value: '2' }],
  });
  assert.deepEqual(first['header'], [
    { key: 'Accept', value: 'application/json' }, { key: 'Authorization', value: '{{authorization}}' },
  ]);
  const second = folders[0]?.item[1]?.request as Record<string, unknown>;
  assert.deepEqual(second['header'], [
    { key: 'Content-Type', value: 'application/json' }, { key: 'Authorization', value: '{{authorization_2}}' },
  ]);
  assert.deepEqual(second['body'], { mode: 'raw', raw: '{"a":1}', options: { raw: { language: 'json' } } });
  const login = folders[1]?.item[0]?.request as Record<string, unknown>;
  assert.deepEqual(login['body'], {
    mode: 'urlencoded', urlencoded: [{ key: 'grant_type', value: 'password' }, { key: 'user', value: 'a' }],
  });
  const values = environment['values'] as { key: string; value: string; type: string }[];
  assert.deepEqual(values.map((v) => [v.key, v.value, v.type]), [
    ['baseUrl_api_example_com', 'https://api.example.com', 'default'],
    ['authorization', 'Bearer secret-1', 'secret'],
    ['authorization_2', 'Bearer secret-2', 'secret'],
    ['baseUrl_login_example_com', 'https://login.example.com', 'default'],
    ['cookie', 's=1', 'secret'],
  ]);
  assert.deepEqual(collection['variable'], [
    { key: 'baseUrl_api_example_com', value: 'https://api.example.com' },
    { key: 'baseUrl_login_example_com', value: 'https://login.example.com' },
  ]);
});
