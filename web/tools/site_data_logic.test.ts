import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  describeServiceWorker, formatBytes, formatExpiry, parseCookies, parseServiceWorkers, parseStorageItems,
  setCookieParams, siteOrigin,
} from './site_data_logic.js';

test('site origins', () => {
  assert.equal(siteOrigin('https://app.example.com:8443/a?b#c'), 'https://app.example.com:8443');
  assert.equal(siteOrigin('http://localhost:8765/x'), 'http://localhost:8765');
  assert.equal(siteOrigin('about:blank'), null);
  assert.equal(siteOrigin('not a url'), null);
});

test('cookies are parsed, sorted and checked', () => {
  const cookies = parseCookies({
    cookies: [
      { name: 'b', value: '2', domain: '.example.com', path: '/', expires: 1900000000, session: false,
        httpOnly: true, secure: true, sameSite: 'Lax' },
      { name: 'a', value: '1', domain: 'example.com', path: '/', expires: -1, session: true },
      { name: 7, value: 'bad' },
    ],
  });
  assert.equal(cookies.length, 2);
  assert.equal(cookies[0]?.name, 'a');
  assert.equal(cookies[0]?.expires, -1);
  assert.equal(cookies[1]?.httpOnly, true);
  assert.deepEqual(parseCookies(null), []);
});

test('storage items', () => {
  assert.deepEqual(parseStorageItems({ entries: [['z', '1'], ['a', '2'], ['bad']] }), [['a', '2'], ['z', '1']]);
  assert.deepEqual(parseStorageItems({}), []);
});

test('formatting', () => {
  assert.equal(formatExpiry(-1), 'Session');
  assert.ok(formatExpiry(0, 'en-US').includes('1970') || formatExpiry(0, 'en-US').includes('1969'));
  assert.equal(formatBytes(512), '512 B');
  assert.equal(formatBytes(1536), '1.5 kB');
  assert.equal(formatBytes(25_000_000), '25 MB');
});

test('setCookie parameters', () => {
  const base = { name: 'n', value: 'v', domain: '', path: '', expires: -1, httpOnly: false, secure: true, sameSite: '' };
  assert.deepEqual(setCookieParams(base, 'https://a.example'), {
    name: 'n', value: 'v', path: '/', secure: true, httpOnly: false, url: 'https://a.example',
  });
  const full = { ...base, domain: '.example.com', path: '/x', expires: 1900000000, sameSite: 'Strict' };
  assert.deepEqual(setCookieParams(full, 'https://a.example'), {
    name: 'n', value: 'v', path: '/x', secure: true, httpOnly: false, domain: '.example.com',
    sameSite: 'Strict', expires: 1900000000,
  });
});

test('service workers of the site', () => {
  const workers = parseServiceWorkers({
    workers: [
      { scope: 'https://app.test/', script: 'https://app.test/sw.js', status: 'activated', running: 'running' },
      { scope: 'https://other.test/', script: 'https://other.test/sw.js', status: 'activated', running: 'stopped' },
      { scope: 'https://app.test/admin/', script: 7, status: 'activated', running: 'running' },
    ],
  }, 'https://app.test');
  assert.deepEqual(workers, [
    { scope: 'https://app.test/', script: 'https://app.test/sw.js', status: 'activated', running: 'running' },
  ]);
  assert.deepEqual(parseServiceWorkers(null, 'https://app.test'), []);
  assert.equal(describeServiceWorker(workers[0]!), '/sw.js · activated, running');
  assert.equal(describeServiceWorker({ scope: 'https://a.test/', script: '', status: '', running: '' }), '');
});
