// Tests for ci_changes.mjs: node --test scripts/*.test.mjs
import assert from 'node:assert/strict';
import test from 'node:test';

import { needsBuild } from './ci_changes.mjs';

test('specs and docs alone need no build', () => {
  assert.equal(needsBuild(['specs/024-new-feature.md', 'specs/README.md']), false);
  assert.equal(needsBuild(['docs/pitch/why-brouk.md', 'docs/testing/history.md', 'README.md', 'CLAUDE.md']), false);
  assert.equal(needsBuild(['.github/ISSUE_TEMPLATE/bug.yml', '.github/pull_request_template.md', '.github/CODEOWNERS']), false);
});

test('anything the app or the build reads needs a build', () => {
  assert.equal(needsBuild(['specs/024-new-feature.md', 'src/core/session.cc']), true);
  // Help is built into the app; licences are read by the licences page.
  assert.equal(needsBuild(['web/help/content/10-profiles.md']), true);
  assert.equal(needsBuild(['third_party/README.md']), true);
  assert.equal(needsBuild(['LICENSE']), true);
  assert.equal(needsBuild(['NOTICE']), true);
  assert.equal(needsBuild(['.github/workflows/linux.yml']), true);
  assert.equal(needsBuild(['scripts/build']), true);
});

test('an empty change builds', () => {
  assert.equal(needsBuild([]), true);
});
