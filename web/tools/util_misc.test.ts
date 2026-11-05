import { test } from 'node:test';
import assert from 'node:assert/strict';
import { hash } from './util_hash.js';
import { explainUuid, formatUuid, uuidV4, uuidV7 } from './util_uuid.js';
import { escapeJson, reformatJson, unescapeJson } from './util_json.js';
import { formatInBase, hexToText, parseInBase, textToHex } from './util_numbers.js';

const text = (s: string): Uint8Array<ArrayBuffer> => new TextEncoder().encode(s);

test('hashes match shasum and openssl', async () => {
  // printf abc | shasum -a 256; printf abc | openssl dgst -sha256 -hmac key
  assert.equal((await hash(text('abc'), 'SHA-256', null)).hex, 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad');
  assert.equal((await hash(text('abc'), 'SHA-1', null)).hex, 'a9993e364706816aba3e25717850c26c9cd0d89d');
  assert.equal((await hash(text('abc'), 'SHA-256', 'key')).hex, '9c196e32dc0175f86f4b1cb89289d6619de6bee699e4c378e68309ed97a1a6ab');
  assert.equal((await hash(text(''), 'SHA-256', null)).base64, '47DEQpj8HBSa+/TImW+5JCeuQeRkm5NMpJWZG3hSuFU=');
});

test('UUIDs', () => {
  assert.equal(explainUuid(uuidV4())?.version, 4);
  const v7 = uuidV7(1730000000000);
  assert.ok(/^[0-9a-f]{8}-[0-9a-f]{4}-7[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/.test(v7));
  assert.deepEqual(explainUuid(v7), { version: 7, variant: 'RFC 9562', time: 1730000000000 });
  // RFC 9562's v1 example: 2022-02-22 19:22:22 UTC.
  assert.equal(explainUuid('C232AB00-9414-11EC-B3C8-9F6BDECED846')?.time, Date.UTC(2022, 1, 22, 19, 22, 22));
  assert.equal(formatUuid('AbC-d', false, false), 'abcd');
  assert.equal(explainUuid('{not-a-uuid}'), null);
});

test('JSON escaping round-trips', () => {
  const sample = 'He said "hi"\\\n\tž😀';
  assert.equal(escapeJson(sample), '"He said \\"hi\\"\\\\\\n\\tž😀"');
  assert.deepEqual(unescapeJson(escapeJson(sample)), { ok: true, text: sample });
  assert.deepEqual(unescapeJson('a\\nb'), { ok: true, text: 'a\nb' });
  assert.deepEqual(reformatJson('{"a":[1,2]}', true), { ok: true, text: '{\n  "a": [\n    1,\n    2\n  ]\n}' });
  assert.deepEqual(reformatJson('{ "a" : 1 }', false), { ok: true, text: '{"a":1}' });
  const bad = reformatJson('{"a":1,}', true);
  assert.ok(/position 7/.test(bad.ok ? '' : bad.error));
});

test('number bases, also above 2^53', () => {
  assert.equal(parseInBase('0xff', 16), 255n);
  assert.equal(parseInBase('1111_1111', 2), 255n);
  assert.equal(parseInBase('-17', 10), -17n);
  assert.equal(parseInBase('19', 8), null);
  const big = parseInBase('18446744073709551615', 10);
  assert.equal(big === null ? '' : formatInBase(big, 16), 'ffffffffffffffff');
  assert.equal(textToHex('Aé'), '41 c3 a9');
  assert.deepEqual(hexToText('41:c3:a9'), { ok: true, text: 'Aé' });
  assert.equal(hexToText('4').ok, false);
  assert.equal(hexToText('ff').ok, false);
});
