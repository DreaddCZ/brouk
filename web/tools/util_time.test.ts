import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  detectUnit, formatInZone, isTimeZone, parseDateText, parseTimestamp, relativeTime, zoneOffsetText,
} from './util_time.js';

test('detects the unit from the size', () => {
  assert.equal(detectUnit(1730000000), 's');
  assert.equal(detectUnit(1730000000000), 'ms');
  assert.equal(detectUnit(1730000000000000), 'us');
  assert.deepEqual(parseTimestamp('1730000000', 'auto'), { ms: 1730000000000, unit: 's' });
  assert.deepEqual(parseTimestamp('1730000000000', 'auto'), { ms: 1730000000000, unit: 'ms' });
  assert.deepEqual(parseTimestamp('1730000000', 'ms'), { ms: 1730000000, unit: 'ms' });
  assert.equal(parseTimestamp('soon', 'auto'), null);
});

test('formats in a time zone', () => {
  assert.equal(formatInZone(1730000000000, 'UTC'), '2024-10-27 03:33:20');
  assert.equal(formatInZone(1730000000000, 'Asia/Tokyo'), '2024-10-27 12:33:20');
  assert.equal(formatInZone(1730000000123, 'UTC'), '2024-10-27 03:33:20.123');
  // 27 October 2024 03:33 UTC is after the switch back to winter time in Prague.
  assert.equal(formatInZone(1730000000000, 'Europe/Prague'), '2024-10-27 04:33:20');
  assert.equal(zoneOffsetText(1730000000000, 'Europe/Prague'), '+01:00');
  assert.equal(zoneOffsetText(1720000000000, 'Europe/Prague'), '+02:00');
  assert.equal(zoneOffsetText(1730000000000, 'America/St_Johns'), '-02:30');
});

test('reads dates back', () => {
  assert.equal(parseDateText('2024-10-27 03:33:20', 'UTC'), 1730000000000);
  assert.equal(parseDateText('2024-10-27T12:33:20', 'Asia/Tokyo'), 1730000000000);
  assert.equal(parseDateText('2024-07-01 12:00', 'Europe/Prague'), Date.UTC(2024, 6, 1, 10));
  assert.equal(parseDateText('2024-10-27T03:33:20Z', 'Asia/Tokyo'), 1730000000000);
  assert.equal(parseDateText('2024-10-27T05:33:20+02:00', 'UTC'), 1730000000000);
  assert.equal(parseDateText('yesterday', 'UTC'), null);
});

test('relative times', () => {
  const now = 1730000000000;
  assert.equal(relativeTime(now - 3 * 3600 * 1000, now), '3 hours ago');
  assert.equal(relativeTime(now + 12 * 60 * 1000, now), 'in 12 minutes');
  assert.equal(relativeTime(now, now), 'now');
});

test('knows time zones', () => {
  assert.equal(isTimeZone('Asia/Tokyo'), true);
  assert.equal(isTimeZone('Mars/Olympus'), false);
});
