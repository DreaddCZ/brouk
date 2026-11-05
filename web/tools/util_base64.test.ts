import { test } from 'node:test';
import assert from 'node:assert/strict';
import { decodeBase64, encodeBase64, fileBase64 } from './util_base64.js';

test('encodes and decodes UTF-8 text', () => {
  assert.equal(encodeBase64('Žluťoučký kůň', false), 'xb1sdcWlb3XEjWvDvSBrxa/FiA==');
  assert.equal(encodeBase64('Žluťoučký kůň', true), 'xb1sdcWlb3XEjWvDvSBrxa_FiA');
  assert.deepEqual(decodeBase64('xb1sdcWlb3XEjWvDvSBrxa/FiA=='), { ok: true, text: 'Žluťoučký kůň' });
  assert.deepEqual(decodeBase64('xb1sdcWlb3XEjWvDvSBrxa_FiA'), { ok: true, text: 'Žluťoučký kůň' });
  assert.deepEqual(decodeBase64(' aGVs\nbG8= '), { ok: true, text: 'hello' });
});

test('reports invalid input with its position', () => {
  const bad = decodeBase64('aGVs*bG8=');
  assert.equal(bad.ok, false);
  assert.ok(/position 5/.test(bad.ok ? '' : bad.error));
  assert.equal(decodeBase64('aGVsb').ok, false);
  assert.equal(decodeBase64('aG=Vs').ok, false);
});

test('shows bytes that are not text as hex', () => {
  const binary = decodeBase64('/w==');
  assert.deepEqual(binary, { ok: true, text: 'ff', note: 'Not text: 1 byte, shown as hex.' });
});

test('gives a file its Base64 and data URL', () => {
  assert.deepEqual(fileBase64(new Uint8Array([104, 105]), 'text/plain'), { base64: 'aGk=', dataUrl: 'data:text/plain;base64,aGk=' });
  assert.equal(fileBase64(new Uint8Array([1]), '').dataUrl, 'data:application/octet-stream;base64,AQ==');
});
