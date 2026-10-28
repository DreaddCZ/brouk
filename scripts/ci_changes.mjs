// CI: whether a change needs the builds (the linux, macos and windows
// workflows). A change to specs and docs only, such as the spec of a future
// feature, skips them; their jobs then report "skipped", which a required
// check counts as passed. Anything else, and anything unclear, builds.
//
//   node scripts/ci_changes.mjs   prints "build=true" or "build=false"
//                                 (for $GITHUB_OUTPUT)
//
// Reads EVENT (github.event_name), BASE and HEAD (the commits to compare)
// from the environment. Only Node's built-in modules: it runs before any
// tool is installed.
import { execFileSync } from 'node:child_process';
import { pathToFileURL } from 'node:url';

// Files no build reads. Not web/help/content (built into the app), not
// third_party/README.md, LICENSE or NOTICE (read by the licences page, 023).
const kDocsOnly = [
  /^specs\//,
  /^docs\//,
  /^[^/]+\.md$/, // README, CONTRIBUTING, SECURITY, CLAUDE.md, CHANGELOG
  /^\.github\/ISSUE_TEMPLATE\//,
  /^\.github\/pull_request_template\.md$/,
  /^\.github\/CODEOWNERS$/,
];

/** True unless every changed file is documentation (and there is one). */
export function needsBuild(files) {
  if (files.length === 0) return true;
  return !files.every((file) => kDocsOnly.some((pattern) => pattern.test(file)));
}

function changedFiles(event, base, head) {
  if (!['pull_request', 'push'].includes(event)) return null;
  // A new branch has no previous commit ("0000…").
  if (!/^[0-9a-f]{40}$/.test(base ?? '') || /^0+$/.test(base) || !/^[0-9a-f]{40}$/.test(head ?? '')) {
    return null;
  }
  try {
    // Three dots: the change itself, from where it branched off.
    const out = execFileSync('git', ['diff', '--name-only', `${base}...${head}`], { encoding: 'utf8' });
    return out.split('\n').filter((line) => line !== '');
  } catch {
    return null;
  }
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  const files = changedFiles(process.env.EVENT, process.env.BASE, process.env.HEAD);
  const build = files === null || needsBuild(files);
  if (files !== null) console.error(`Changed files: ${files.length}${build ? '' : ' (specs and docs only)'}`);
  console.log(`build=${build}`);
}
