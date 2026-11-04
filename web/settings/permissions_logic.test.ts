import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BridgeError } from '../shared/bridge.js';
import { PermissionsApi, parsePermissionsState } from './permissions_api.js';
import { filterSites, siteLabel } from './permissions_logic.js';

const answer = {
  sites: [
    { origin: 'https://meet.example.com', permissions: [{ id: 'media_stream_camera', setting: 'allow' }] },
    { origin: 'http://localhost:8765', permissions: [{ id: 'geolocation', setting: 'block' }] },
  ],
  kinds: [{ id: 'media_stream_camera', name: 'Camera' }],
};

test('parsePermissionsState checks the answer', () => {
  const state = parsePermissionsState(answer);
  assert.equal(state.sites.length, 2);
  assert.equal(state.sites[0]?.permissions[0]?.setting, 'allow');
  assert.equal(state.kinds[0]?.name, 'Camera');
  const wrong = { ...answer, sites: [{ origin: 'https://a.example', permissions: [{ id: 'x', setting: 'ask' }] }] };
  assert.throws(() => parsePermissionsState(wrong), BridgeError);
  assert.throws(() => parsePermissionsState({ sites: 'none', kinds: [] }), BridgeError);
  assert.throws(() => parsePermissionsState(null), BridgeError);
});

test('siteLabel hides https only', () => {
  assert.equal(siteLabel('https://example.com'), 'example.com');
  assert.equal(siteLabel('http://localhost:8765'), 'http://localhost:8765');
});

test('filterSites matches the address', () => {
  const sites = parsePermissionsState(answer).sites;
  assert.equal(filterSites(sites, '').length, 2);
  assert.deepEqual(
    filterSites(sites, ' MEET ').map((s) => s.origin),
    ['https://meet.example.com'],
  );
  assert.equal(filterSites(sites, 'nothing').length, 0);
});

test('PermissionsApi sends the profile and the choice', async () => {
  const sent: unknown[] = [];
  const api = new PermissionsApi(async (request) => {
    sent.push(JSON.parse(request));
    return '';
  });
  await api.set('default', 'https://example.com', 'geolocation', 'ask');
  await api.reset('default', 'https://example.com');
  assert.deepEqual(sent, [
    { type: 'permissions.set', profile: 'default', origin: 'https://example.com', id: 'geolocation', setting: 'ask' },
    { type: 'permissions.reset', profile: 'default', origin: 'https://example.com' },
  ]);
});
