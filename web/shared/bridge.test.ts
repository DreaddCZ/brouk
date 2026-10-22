import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BridgeError, send } from './bridge.js';

test('send adds the type and parses the answer', async () => {
  let sent = '';
  const answer = await send('profiles.create', { name: 'Work' }, async (request) => {
    sent = request;
    return '{"id":"p-1"}';
  });
  assert.deepEqual(JSON.parse(sent), { name: 'Work', type: 'profiles.create' });
  assert.deepEqual(answer, { id: 'p-1' });
});

test('the type cannot be overridden by the arguments', async () => {
  let sent = '';
  await send('a', { type: 'b' }, async (request) => {
    sent = request;
    return '';
  });
  assert.equal(JSON.parse(sent).type, 'a');
});

test('an empty answer is null; a broken one is an error', async () => {
  assert.equal(await send('a', {}, async () => ''), null);
  await assert.rejects(send('a', {}, async () => '{'), BridgeError);
});

test('outside Brouk there is no transport', async () => {
  await assert.rejects(send('a', {}, null), /only inside Brouk/);
});
