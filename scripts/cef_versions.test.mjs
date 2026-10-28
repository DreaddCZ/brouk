// Tests for cef_versions.mjs: node --test scripts/*.test.mjs
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';

import { assess, parseVersion, PLATFORMS, stableReleases } from './cef_versions.mjs';

const v = (cef, chromium = '1.0.0.0') => `${cef}+gabc1234+chromium-${chromium}`;

/** An index with the given [version, channel, released-ISO-date] for all platforms. */
function index(entries, platforms = PLATFORMS) {
  const result = {};
  for (const platform of platforms) {
    result[platform] = {
      versions: entries.map(([version, channel, date]) => ({
        cef_version: version,
        channel,
        chromium_version: version.split('chromium-')[1],
        files: [
          { type: 'standard', name: `std_${platform}`, sha1: 'aa', last_modified: date },
          { type: 'minimal', name: `cef_binary_${version}_${platform}_minimal.tar.bz2`, sha1: SHA1, last_modified: date },
        ],
      })),
    };
  }
  return result;
}

const SHA1 = 'bc46c83878741fa2b23486940dacebabb294bf39';
const NOW = new Date('2026-10-28T12:00:00Z');

test('parses CEF versions and rejects anything else', () => {
  assert.deepEqual(parseVersion(v('156.0.14')), [156, 0, 14]);
  assert.equal(parseVersion('156.0.14'), null);
  assert.equal(parseVersion(`${v('156.0.14')}; rm -rf /`), null);
});

test('orders releases numerically and skips beta and incomplete ones', () => {
  const idx = index([
    [v('154.0.9'), 'stable', '2026-10-01T00:00:00Z'],
    [v('154.0.34'), 'stable', '2026-10-02T00:00:00Z'],
    [v('156.0.1'), 'beta', '2026-10-03T00:00:00Z'],
  ]);
  idx.linux64.versions.push({
    cef_version: v('158.0.1'), channel: 'stable',
    files: [{ type: 'minimal', name: `cef_binary_${v('158.0.1')}_linux64_minimal.tar.bz2`, sha1: SHA1, last_modified: '2026-10-04T00:00:00Z' }],
  });
  assert.deepEqual(
    stableReleases(idx).map((r) => r.version),
    [v('154.0.34'), v('154.0.9')],
  );
});

test('ignores files with unexpected names or checksums', () => {
  const idx = index([[v('156.0.7'), 'stable', '2026-10-01T00:00:00Z']]);
  idx.linux64.versions[0].files[1].name = '../../evil.tar.bz2';
  assert.deepEqual(stableReleases(idx), []);
  const bad = index([[v('156.0.7'), 'stable', '2026-10-01T00:00:00Z']]);
  bad.macosx64.versions[0].files[1].sha1 = 'not-a-sha1';
  assert.deepEqual(stableReleases(bad), []);
});

test('up to date', () => {
  const result = assess(index([[v('154.0.34'), 'stable', '2026-10-01T00:00:00Z']]), v('154.0.34'), NOW);
  assert.equal(result.overdue, false);
  assert.equal(result.latest, null);
});

test('a new patch is fine for 7 days, then overdue', () => {
  const pinned = v('154.0.33');
  const fresh = index([[pinned, 'stable', '2026-10-01T00:00:00Z'], [v('154.0.34'), 'stable', '2026-10-22T00:00:00Z']]);
  assert.equal(assess(fresh, pinned, NOW).overdue, false);
  assert.equal(assess(fresh, pinned, NOW).latest.version, v('154.0.34'));
  const old = index([[pinned, 'stable', '2026-10-01T00:00:00Z'], [v('154.0.34'), 'stable', '2026-10-20T00:00:00Z']]);
  assert.equal(assess(old, pinned, NOW).overdue, true);
});

test('the clock starts with the first newer patch', () => {
  const pinned = v('154.0.30');
  const idx = index([
    [v('154.0.31'), 'stable', '2026-10-01T00:00:00Z'],
    [v('154.0.34'), 'stable', '2026-10-27T00:00:00Z'],
  ]);
  assert.equal(assess(idx, pinned, NOW).overdue, true);
});

test('one newer major is fine for 28 days, then overdue', () => {
  const pinned = v('154.0.34');
  const fresh = index([[v('156.0.7'), 'stable', '2026-10-10T00:00:00Z']]);
  assert.equal(assess(fresh, pinned, NOW).overdue, false);
  const old = index([[v('156.0.7'), 'stable', '2026-09-20T00:00:00Z']]);
  assert.equal(assess(old, pinned, NOW).overdue, true);
});

test('two newer majors are always overdue', () => {
  const idx = index([
    [v('156.0.7'), 'stable', '2026-10-20T00:00:00Z'],
    [v('158.0.1'), 'stable', '2026-10-27T00:00:00Z'],
  ]);
  const result = assess(idx, v('154.0.34'), NOW);
  assert.equal(result.overdue, true);
  assert.equal(result.latest.version, v('158.0.1'));
});

test('an older extended-support branch does not count as newer', () => {
  const idx = index([[v('144.0.36'), 'stable', '2026-10-27T00:00:00Z']]);
  assert.equal(assess(idx, v('154.0.34'), NOW).latest, null);
});

test('an unreadable pinned version is reported', () => {
  assert.equal(assess(index([]), 'garbage', NOW).overdue, true);
});

test('every platform fetch_cef has a checksum for is updated', () => {
  // A platform missing here keeps the old version's checksum (CEF 156: Windows).
  const fetchCef = readFileSync(new URL('./fetch_cef', import.meta.url), 'utf8');
  const pinned = [...fetchCef.matchAll(/^ {4}([a-z0-9]+)\) echo [0-9a-f]{64} ;;$/gm)].map((m) => m[1]);
  assert.ok(pinned.length > 0);
  assert.deepEqual([...PLATFORMS].sort(), pinned.sort());
});
