import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  findingsMarkdown, headerValues, markdownFileName, isSessionCookie, parsePolicy, prefixProblem, runSecurityChecks, shorten, summary,
  type CheckedResponse, type HeaderList, type SecurityInput,
} from './security_logic.js';
import type { Cookie } from './site_data_logic.js';

const kSessionNames = 'session, sess, sid, auth, token, jwt, JSESSIONID, PHPSESSID, ASP.NET_SessionId, .AspNetCore., connect.sid, laravel_session';
const kSecretParameters = 'access_token, id_token, refresh_token, token, session, sessionid, sid, password, api_key, secret';
const kJwt = 'eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxMjMifQ.c2lnbmF0dXJlLXNpZ25hdHVyZQ';
const kNow = 1_800_000_000;

function cookie(name: string, extra: Partial<Cookie> = {}): Cookie {
  return {
    name, value: 'abc', domain: 'app.example.com', path: '/', expires: -1, httpOnly: false, secure: false,
    sameSite: '', ...extra,
  };
}

function response(url: string, responseHeaders: HeaderList, requestHeaders: HeaderList = []): CheckedResponse {
  return { url, status: 200, requestHeaders, responseHeaders };
}

const kGoodHeaders: HeaderList = [
  ['Content-Security-Policy', "default-src 'self'; script-src 'self'; style-src 'self'; object-src 'none'; base-uri 'self'; form-action 'self'; frame-ancestors 'self'"],
  ['Strict-Transport-Security', 'max-age=31536000; includeSubDomains'],
  ['X-Content-Type-Options', 'nosniff'],
  ['Referrer-Policy', 'strict-origin-when-cross-origin'],
  ['Cross-Origin-Opener-Policy', 'same-origin'],
  ['Permissions-Policy', 'camera=()'],
  ['Server', 'nginx'],
];

function input(extra: Partial<SecurityInput> = {}): SecurityInput {
  const url = extra.url ?? 'https://app.example.com/';
  return {
    url, site: 'example.com', document: response(url, kGoodHeaders), siteRequests: [], pageRequests: [], cookies: [],
    storage: [],
    elements: { formActions: [], passwordFields: 0, subresources: [], loads: [], metaPolicies: [] },
    sessionCookies: kSessionNames, secretParameters: kSecretParameters, now: kNow, ...extra,
  };
}

const ids = (i: SecurityInput): string[] => runSecurityChecks(i).findings.map((f) => f.id);

test('session cookie names: whole names, words and prefixes', () => {
  assert.ok(isSessionCookie('session', 'x', kSessionNames));
  assert.ok(isSessionCookie('__Host-session', 'x', kSessionNames));
  assert.ok(isSessionCookie('my_sid', 'x', kSessionNames));
  assert.ok(isSessionCookie('authToken', 'x', kSessionNames));
  assert.ok(isSessionCookie('ASP.NET_SessionId', 'x', kSessionNames));
  assert.ok(isSessionCookie('.AspNetCore.Cookies', 'x', kSessionNames));
  assert.ok(isSessionCookie('prefs', kJwt, kSessionNames), 'a JWT value counts');
  assert.ok(!isSessionCookie('theme', 'dark', kSessionNames));
  assert.ok(!isSessionCookie('sidebar_state', 'open', kSessionNames));
});

test('a page with no security headers and flagless cookies (acceptance criterion)', () => {
  const url = 'https://app.example.com/';
  const found = ids(input({
    url, document: response(url, []),
    cookies: [cookie('session'), cookie('theme', { value: 'dark' })],
  }));
  for (const id of ['csp-missing', 'hsts-missing', 'clickjacking', 'nosniff:document', 'referrer-policy',
    'cookie-secure:session', 'cookie-httponly:session', 'cookie-samesite:session',
    'cookie-secure:theme', 'cookie-samesite:theme']) {
    assert.ok(found.includes(id), `${id} in ${found.join(' ')}`);
  }
  assert.ok(!found.includes('cookie-httponly:theme'), 'theme is not a session cookie');
});

test('a page with good headers and a __Host- session cookie has no high or medium findings', () => {
  const result = runSecurityChecks(input({
    cookies: [cookie('__Host-session', { secure: true, httpOnly: true, sameSite: 'Lax', domain: 'app.example.com' })],
  }));
  const serious = result.findings.filter((f) => f.severity === 'high' || f.severity === 'medium');
  assert.deepEqual(serious, []);
  assert.deepEqual(result.findings, []);
  assert.ok(result.passed.includes('A Content-Security-Policy is set'));
  assert.ok(result.passed.includes('Session cookies are not readable by scripts (HttpOnly)'));
});

test('severities follow ZAP', () => {
  const url = 'https://app.example.com/';
  const result = runSecurityChecks(input({ url, document: response(url, []), cookies: [cookie('session')] }));
  const severity = (id: string): string | undefined => result.findings.find((f) => f.id === id)?.severity;
  assert.equal(severity('csp-missing'), 'medium');  // 10038
  assert.equal(severity('clickjacking'), 'medium');  // 10020
  assert.equal(severity('hsts-missing'), 'low');  // 10035
  assert.equal(severity('nosniff:document'), 'low');  // 10021
  assert.equal(severity('cookie-httponly:session'), 'low');  // 10010
  assert.equal(severity('coop'), 'low');  // 90004
  assert.ok(result.findings.every((f) => f.rating !== '' && f.link.startsWith('https://')));
  // Sorted by severity.
  const order = result.findings.map((f) => ['high', 'medium', 'low', 'info'].indexOf(f.severity));
  assert.deepEqual(order, [...order].sort((a, b) => a - b));
});

test('weak and report-only policies', () => {
  const url = 'https://app.example.com/';
  const weak = ids(input({
    url, document: response(url, [...kGoodHeaders.filter(([n]) => n !== 'Content-Security-Policy'),
      ['Content-Security-Policy', "script-src 'self' 'unsafe-inline'; frame-ancestors 'self'"]]),
  }));
  assert.ok(weak.includes('csp-weak:script-inline'));
  assert.ok(weak.includes('csp-directive:base-uri'));
  assert.ok(weak.includes('csp-directive:object-src'));
  assert.ok(!weak.includes('clickjacking'), 'frame-ancestors protects');
  // A nonce disables 'unsafe-inline'.
  const nonce = ids(input({
    url, document: response(url, [...kGoodHeaders.filter(([n]) => n !== 'Content-Security-Policy'),
      ['Content-Security-Policy', "default-src 'self'; script-src 'self' 'unsafe-inline' 'nonce-abc'; style-src 'self'; object-src 'none'; base-uri 'self'; form-action 'self'; frame-ancestors 'none'"]]),
  }));
  assert.ok(!nonce.includes('csp-weak:script-inline'));
  const reportOnly = runSecurityChecks(input({
    url, document: response(url, [['Content-Security-Policy-Report-Only', "default-src 'self'"]]),
  }));
  const found = reportOnly.findings.map((f) => f.id);
  assert.ok(found.includes('csp-report-only'));
  assert.ok(found.includes('csp-missing'));
  assert.equal(reportOnly.findings.find((f) => f.id === 'csp-report-only')?.severity, 'info');
});

test('a policy in a meta element counts', () => {
  const url = 'https://app.example.com/';
  const found = ids(input({
    url, document: response(url, kGoodHeaders.filter(([n]) => n !== 'Content-Security-Policy')),
    elements: { formActions: [], passwordFields: 0, subresources: [], loads: [],
      metaPolicies: ["default-src 'self'; object-src 'none'; base-uri 'self'; form-action 'self'"] },
  }));
  assert.ok(!found.includes('csp-missing'));
  assert.ok(found.includes('csp-directive:frame-ancestors'), 'meta policies cannot set frame-ancestors, but it is still missing');
});

test('version disclosure', () => {
  const url = 'https://app.example.com/';
  const result = runSecurityChecks(input({
    url, document: response(url, [...kGoodHeaders.filter(([n]) => n !== 'Server'), ['Server', 'nginx/1.25.3'],
      ['X-Powered-By', 'Express']]),
  }));
  const found = result.findings.map((f) => f.id);
  assert.ok(found.includes('version:server'));
  assert.ok(found.includes('version:x-powered-by'));
  assert.ok(!ids(input()).includes('version:server'), 'Server: nginx without a version is fine');
});

test('CORS with credentials on the site\'s APIs', () => {
  const api = (allow: string, origin = 'https://app.example.com'): CheckedResponse => response(
    'https://api.example.com/me', [['Access-Control-Allow-Origin', allow], ['Access-Control-Allow-Credentials', 'true'],
      ['X-Content-Type-Options', 'nosniff']], [['Origin', origin]]);
  const result = runSecurityChecks(input({ siteRequests: [api('https://app.example.com')] }));
  assert.equal(result.findings.find((f) => f.id === 'cors:echo')?.severity, 'info');
  assert.equal(runSecurityChecks(input({ siteRequests: [api('null')] })).findings.find((f) => f.id === 'cors:null')?.severity,
    'high');
  assert.ok(ids(input({ siteRequests: [api('*')] })).includes('cors:wildcard'));
  // Without credentials, nothing to report.
  assert.ok(!ids(input({
    siteRequests: [response('https://api.example.com/x', [['Access-Control-Allow-Origin', '*'], ['X-Content-Type-Options', 'nosniff']])],
  })).some((id) => id.startsWith('cors')));
});

test('API responses: nosniff and caching of signed-in responses', () => {
  const found = ids(input({
    siteRequests: [response('https://app.example.com/api/me', [['Cache-Control', 'private']], [['Authorization', 'Bearer x']]),
      response('https://app.example.com/api/public', [['X-Content-Type-Options', 'nosniff']])],
  }));
  assert.ok(found.includes('nosniff:api'));
  assert.ok(found.includes('cache-authenticated'));
  assert.ok(!ids(input({
    siteRequests: [response('https://app.example.com/api/me', [['Cache-Control', 'no-store'], ['X-Content-Type-Options', 'nosniff']],
      [['Authorization', 'Bearer x']])],
  })).includes('cache-authenticated'));
});

test('secrets in URLs, mixed content and forms posting to HTTP', () => {
  const result = runSecurityChecks(input({
    url: 'https://app.example.com/cb#access_token=abcdefghijklmnop',
    pageRequests: [
      { url: 'https://app.example.com/login?user=a&password=hunter2hunter2', type: 'Fetch', error: '' },
      { url: 'http://cdn.example.net/lib.js', type: 'Script', error: 'mixed-content' },
    ],
    elements: { formActions: ['http://app.example.com/save', '/ok'], passwordFields: 1, subresources: [], loads: [],
      metaPolicies: [] },
  }));
  const byId = new Map(result.findings.map((f) => [f.id, f]));
  assert.equal(byId.get('url-secret:access_token')?.severity, 'medium');
  assert.equal(byId.get('url-secret:password')?.severity, 'info');
  assert.ok(!byId.get('url-secret:password')?.found.includes('hunter2hunter2'), 'values are shortened');
  assert.ok(byId.has('mixed-content'));
  assert.ok(byId.has('form-http'));
});

test('HTTP pages, and local development pages checked as if HTTPS', () => {
  const plain = ids(input({
    url: 'http://app.example.com/', document: response('http://app.example.com/', kGoodHeaders),
    elements: { formActions: [], passwordFields: 1, subresources: [], loads: [], metaPolicies: [] },
  }));
  assert.ok(plain.includes('http-page'));
  assert.ok(plain.includes('password-http'));
  const local = runSecurityChecks(input({
    url: 'http://localhost:8000/', site: 'localhost', document: response('http://localhost:8000/', []),
    cookies: [cookie('session', { domain: 'localhost' })],
  }));
  const found = local.findings.map((f) => f.id);
  assert.ok(!found.includes('http-page'));
  assert.ok(found.includes('hsts-missing'));
  assert.ok(found.includes('cookie-secure:session'));
  assert.ok(local.notes.some((n) => n.includes('as if it were served over HTTPS')));
});

test('session cookies: scope, lifetime and prefixes', () => {
  const found = ids(input({
    cookies: [cookie('sid', { domain: '.example.com', secure: true, httpOnly: true, sameSite: 'Lax',
      expires: kNow + 90 * 86400 })],
    document: response('https://app.example.com/', [...kGoodHeaders, ['Set-Cookie', '__Host-auth=1; Path=/app; Secure\n__Secure-x=2']]),
  }));
  assert.ok(found.includes('cookie-scope:sid'));
  assert.ok(found.includes('cookie-lifetime:sid'));
  assert.ok(found.includes('cookie-prefix:__Host-auth'));
  assert.ok(found.includes('cookie-prefix:__Secure-x'));
  assert.equal(prefixProblem('__Host-ok=1; Secure; Path=/'), null);
  assert.equal(prefixProblem('plain=1'), null);
  assert.equal(prefixProblem('__Host-a=1; Secure; Path=/; Domain=example.com')?.problem, '__Host- cookies must not set Domain.');
});

test('third-party scripts without integrity and JWTs in storage', () => {
  const found = ids(input({
    elements: { formActions: [], passwordFields: 0, metaPolicies: [], loads: [],
      subresources: [{ url: 'https://cdn.other.net/lib.js', integrity: '' },
        { url: 'https://cdn.other.net/ok.js', integrity: 'sha384-abc' },
        { url: 'https://static.example.com/app.js', integrity: '' }] },
    storage: [{ area: 'local', key: 'auth', value: JSON.stringify({ token: kJwt }) }, { area: 'session', key: 'x', value: '1' }],
  }));
  assert.ok(found.includes('sri'));
  assert.ok(found.includes('jwt-storage:local:auth'));
  assert.ok(!found.includes('jwt-storage:session:x'));
});

test('no recorded page load: headers are not checked and a note says why', () => {
  const result = runSecurityChecks(input({ document: null }));
  assert.ok(!result.findings.some((f) => f.id === 'csp-missing'));
  assert.ok(result.notes.some((n) => n.includes('Reload and check')));
});

test('summary and Markdown leave hidden findings out and shorten secrets', () => {
  const url = 'https://app.example.com/';
  const result = runSecurityChecks(input({
    url, document: response(url, []), cookies: [cookie('session', { value: kJwt })],
  }));
  const hidden = new Set(['csp-missing']);
  assert.ok(summary(result, hidden).includes('1 hidden'));
  const markdown = findingsMarkdown(result, url, hidden, new Date(Date.UTC(2026, 10, 5)));
  assert.ok(markdown.startsWith(`# Security check: ${url}`));
  assert.ok(markdown.includes('## Medium'));
  assert.ok(!markdown.includes('No Content-Security-Policy'));
  assert.ok(!markdown.includes(kJwt), 'the token is shortened');
  assert.ok(markdown.includes(shorten(kJwt)));
});

test('helpers', () => {
  assert.deepEqual(headerValues([['Set-Cookie', 'a=1\nb=2'], ['set-cookie', 'c=3']], 'SET-COOKIE'), ['a=1', 'b=2', 'c=3']);
  const policy = parsePolicy("default-src 'self'; SCRIPT-SRC https://x; script-src 'none'");
  assert.deepEqual(policy.get('script-src'), ['https://x']);
  assert.equal(shorten('short'), 'short');
  assert.equal(shorten('abcdefghijklmnop'), 'abcdefgh…');
});

test('Markdown file name', () => {
  const at = new Date(Date.UTC(2026, 10, 5, 12));
  assert.equal(markdownFileName('https://app.example.com/x?y', at), 'security-check-app.example.com-2026-11-05.md');
  assert.equal(markdownFileName('http://127.0.0.1:8794/bad', at), 'security-check-127.0.0.1-2026-11-05.md');
  assert.ok(/^security-check-[a-z0-9.-]+-2026-11-05\.md$/.test(markdownFileName('http://[::1]:8000/', at)));
});
