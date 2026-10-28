// CEF versions from CEF's build index (cef-builds.spotifycdn.com/index.json)
// and the update policy of specs/001 ("Update process"):
//   - a newer patch of our CEF major is taken within 7 days,
//   - a newer major within 28 days (one Chromium cycle), and we are never
//     more than one major behind.
//
//   node scripts/cef_versions.mjs check <index.json> <pinned-version>
//       prints the state; exit 1 if an update is overdue
//   node scripts/cef_versions.mjs files <index.json> <version|latest>
//       prints "<platform> <file name> <sha1>" of the minimal distributions
//       we use, one line per platform (exit 1 if one is missing)
//
// The network access stays in the shell scripts (check_cef, update_cef);
// everything here is a pure function of the index, so it is unit-tested.
import fs from 'node:fs';
import { pathToFileURL } from 'node:url';

/** The platforms we build, as named in the index. */
export const PLATFORMS = ['macosx64', 'macosarm64', 'linux64'];
export const PATCH_DAYS = 7;
export const MAJOR_DAYS = 28;
const DAY_MS = 24 * 60 * 60 * 1000;

/** "156.0.14+g4fa4ab5+chromium-156.0.8078.48" -> [156, 0, 14], or null. */
export function parseVersion(text) {
  const match = /^(\d+)\.(\d+)\.(\d+)\+g[0-9a-f]+\+chromium-[\d.]+$/.exec(text);
  return match ? [Number(match[1]), Number(match[2]), Number(match[3])] : null;
}

export function compareVersions(a, b) {
  for (let i = 0; i < 3; i++) {
    if (a[i] !== b[i]) return a[i] - b[i];
  }
  return 0;
}

/**
 * Stable releases that have a minimal distribution for every platform we
 * build, newest first: [{version, parsed, chromium, files, released}], where
 * files maps platform -> {name, sha1} and released is when the last of
 * those files appeared (a Date).
 */
export function stableReleases(index) {
  const byVersion = new Map();
  for (const platform of PLATFORMS) {
    for (const entry of index?.[platform]?.versions ?? []) {
      if (entry.channel !== 'stable') continue;
      const parsed = parseVersion(entry.cef_version);
      // The name becomes a download URL and a file name: only the expected one.
      const name = `cef_binary_${entry.cef_version}_${platform}_minimal.tar.bz2`;
      const file = (entry.files ?? []).find(
        (f) => f.type === 'minimal' && f.name === name && /^[0-9a-f]{40}$/.test(f.sha1),
      );
      if (!parsed || !file) continue;
      let release = byVersion.get(entry.cef_version);
      if (!release) {
        release = {
          version: entry.cef_version,
          parsed,
          chromium: entry.chromium_version,
          files: {},
          released: new Date(0),
        };
        byVersion.set(entry.cef_version, release);
      }
      release.files[platform] = { name: file.name, sha1: file.sha1 };
      const modified = new Date(file.last_modified);
      if (modified > release.released) release.released = modified;
    }
  }
  return [...byVersion.values()]
    .filter((r) => PLATFORMS.every((p) => r.files[p]))
    .sort((a, b) => compareVersions(b.parsed, a.parsed));
}

function days(from, to) {
  return Math.floor((to - from) / DAY_MS);
}

/**
 * How the pinned version stands against the policy:
 * {overdue, lines, latest}, where lines explain it for people and latest is
 * the release to update to (null if none is newer).
 */
export function assess(index, pinned, now) {
  const current = parseVersion(pinned);
  if (!current) {
    return { overdue: true, lines: [`Cannot read the pinned CEF version "${pinned}".`], latest: null };
  }
  const releases = stableReleases(index);
  const newer = releases.filter((r) => compareVersions(r.parsed, current) > 0);
  if (newer.length === 0) {
    return { overdue: false, lines: [`CEF ${pinned} is the newest stable release.`], latest: null };
  }
  const lines = [`Pinned: CEF ${pinned}.`];
  let overdue = false;

  // The overdue clock starts with the first newer release, not the newest.
  const firstOf = (list) => list.reduce((a, b) => (a.released < b.released ? a : b));
  const patches = newer.filter((r) => r.parsed[0] === current[0]);
  if (patches.length > 0) {
    const age = days(firstOf(patches).released, now);
    const late = age > PATCH_DAYS;
    overdue ||= late;
    lines.push(
      `${late ? 'OVERDUE' : 'Update'}: patch ${patches[0].version} ` +
        `(a newer patch has been out ${age} days; policy: ${PATCH_DAYS}).`,
    );
  }
  const majors = [...new Set(newer.map((r) => r.parsed[0]))].filter((m) => m > current[0]);
  if (majors.length > 0) {
    const nextMajor = Math.min(...majors);
    const age = days(firstOf(newer.filter((r) => r.parsed[0] === nextMajor)).released, now);
    const late = age > MAJOR_DAYS || majors.length > 1;
    overdue ||= late;
    lines.push(
      `${late ? 'OVERDUE' : 'Update'}: ${majors.length} newer major ` +
        `version${majors.length > 1 ? 's' : ''}, newest ${newer[0].version} ` +
        `(CEF ${nextMajor} has been out ${age} days; policy: ${MAJOR_DAYS}, ` +
        'and never more than one major behind).',
    );
  }
  lines.push('Update with: scripts/update_cef (see docs/cef-updates.md).');
  return { overdue, lines, latest: newer[0] };
}

function main(argv) {
  const [command, indexPath, version] = argv;
  if (!['check', 'files'].includes(command) || !indexPath || !version) {
    console.error('usage: cef_versions.mjs check|files <index.json> <version>');
    return 2;
  }
  const index = JSON.parse(fs.readFileSync(indexPath, 'utf8'));
  if (command === 'check') {
    const result = assess(index, version, new Date());
    for (const line of result.lines) console.log(line);
    return result.overdue ? 1 : 0;
  }
  const releases = stableReleases(index);
  const release =
    version === 'latest' ? releases[0] : releases.find((r) => r.version === version);
  if (!release) {
    console.error(`No stable CEF ${version} with all of: ${PLATFORMS.join(', ')}.`);
    return 1;
  }
  for (const platform of PLATFORMS) {
    const file = release.files[platform];
    console.log(`${platform} ${file.name} ${file.sha1}`);
  }
  return 0;
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  process.exitCode = main(process.argv.slice(2));
}
