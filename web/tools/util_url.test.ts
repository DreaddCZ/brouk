import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildUrl, decodeUrl, encodeUrl, parseUrl } from './util_url.js';
import { decodeHtml, encodeHtml } from './util_html.js';

test('encodes URL text three ways', () => {
  assert.deepEqual(encodeUrl('a b&c=d/é', 'component'), { ok: true, text: 'a%20b%26c%3Dd%2F%C3%A9' });
  assert.deepEqual(encodeUrl('a b&c=d/é', 'form'), { ok: true, text: 'a+b%26c%3Dd%2F%C3%A9' });
  assert.deepEqual(encodeUrl('https://example.com/a b?q=é#x', 'url'), { ok: true, text: 'https://example.com/a%20b?q=%C3%A9#x' });
  assert.equal(encodeUrl('\uD800', 'component').ok, false);
});

test('decodes and reports invalid sequences', () => {
  assert.deepEqual(decodeUrl('a%20b%26c%3Dd%2F%C3%A9', 'component'), { ok: true, text: 'a b&c=d/é' });
  assert.deepEqual(decodeUrl('a+b%2B', 'form'), { ok: true, text: 'a b+' });
  assert.deepEqual(decodeUrl('a+b', 'component'), { ok: true, text: 'a+b' });
  const lone = decodeUrl('caf%E9', 'component');
  assert.equal(lone.ok, false);
  assert.ok(/position 4.*UTF-8/.test(lone.ok ? '' : lone.error));
  const bad = decodeUrl('x%G1', 'component');
  assert.ok(/position 2/.test(bad.ok ? '' : bad.error));
});

test('parses and rebuilds URLs', () => {
  const parsed = parseUrl('https://user@example.com:8443/a/b?q=a%20b&q=2&x=%C3%A9#top');
  assert.ok('parts' in parsed);
  if (!('parts' in parsed)) return;
  assert.equal(parsed.parts.hostname, 'example.com');
  assert.equal(parsed.parts.port, '8443');
  assert.deepEqual(parsed.parts.params, [['q', 'a b'], ['q', '2'], ['x', 'é']]);
  assert.equal(parsed.parts.hash, 'top');
  const rebuilt = buildUrl({ ...parsed.parts, params: [['q', 'c&d'], ['new', '1']] });
  assert.equal(rebuilt, 'https://user@example.com:8443/a/b?q=c%26d&new=1#top');
  assert.ok('error' in parseUrl('example.com/x'));
});

test('HTML entities', () => {
  assert.equal(encodeHtml('<a href="x">&</a>', false), '&lt;a href=&quot;x&quot;&gt;&amp;&lt;/a&gt;');
  assert.equal(encodeHtml('é😀', true), '&#233;&#128512;');
  assert.equal(decodeHtml('&lt;a href=&quot;x&quot;&gt;&amp;&lt;/a&gt;'), '<a href="x">&</a>');
  assert.equal(decodeHtml('&eacute;&#233;&#xE9;'), 'ééé');
  assert.equal(decodeHtml('&unknown; &#0;'), '&unknown; �');
  assert.equal(decodeHtml('&constructor; &toString; &Omega;&hearts;&apos;'), '&constructor; &toString; Ω♥\'');
});
