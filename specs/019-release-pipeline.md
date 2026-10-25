# 019 – Release pipeline: local build, remote tests, publish

- **Status:** draft
- **Priority:** P2
- **Depends on:** 001, 002, 006 (remote debugging flag), 012 (crash page)

## Goal

One command on the user's Mac builds a release of Brouk and uploads it to GitHub as a hidden draft. GitHub Actions then tests it on clean Macs it has never seen before. Only a build that passes every test becomes a release colleagues can download, and **the files that were tested are exactly the files that are published** — nothing is rebuilt after testing.

Builds stay local (as decided in 001, phase 1), so macOS CI minutes are spent only on short tests, not on builds. The pipeline works on the private repository first and keeps working unchanged after the repository (or a separate releases repository, 001 "Update process" point 4) becomes public.

Scope now: macOS, Apple Silicon and Intel (002). Windows joins later as more test jobs.

## Description

### Versions and release states

- The version stays in **one place**: `APP_VERSION` in `CMakeLists.txt` (see `CLAUDE.md`, Decisions). The git tag is `v<APP_VERSION>`, for example `v0.4.0`.
- The app shows the version together with the short commit hash, for example `0.4.0 (a1b2c3d)`, in About and in `brouk --version`, so every report (018) points to an exact build.
- While colleagues beta test, versions are `0.x` and every published release is a pre-release.

| State | Who can see it | How it gets there |
|---|---|---|
| Draft | Only the user (repo admins and writers) | The release script uploads the build |
| Pre-release | Everyone with access to the repo; not offered as "latest" | Tests pass, released with `--beta` |
| Latest | Everyone with access; the default download | Tests pass, released without `--beta` |
| Discarded | Nobody | Tests failed and the user discarded the draft; its tag goes with it |

- A discarded version number may be reused after the fix, because nobody but the user ever saw the draft.
- A published version is never replaced or re-uploaded. A fix after publishing gets a new version number.

### Local release script

`scripts/release [--beta]` runs on the user's Apple Silicon Mac and does everything up to the tests. It stops with a clear message at the first failed step and leaves nothing half-published.

1. **Check preconditions:** on `main`; no uncommitted changes; `main` pushed to GitHub; tag `v<APP_VERSION>` does not exist yet; the `gh` CLI is signed in; `CHANGELOG.md` has a section for this version.
2. **Build** the Release configuration for both architectures with `scripts/build --release --arch arm64` and `--arch x86_64` (the Intel build is cross-compiled, as decided in 001), and run the core tests of the native build.
3. **Sign** both apps the same way local builds are signed today (the "Brouk Local Dev" certificate from `scripts/create_dev_certificate`, 001 Notes). From phase 2: Developer ID signing and notarization.
4. **Package** each app as a zip that keeps the bundle intact (`ditto -c -k --keepParent`):
   - `Brouk-<version>-macos-arm64.zip`
   - `Brouk-<version>-macos-x64.zip`
   - `SHA256SUMS.txt` with the checksums of both zips
5. **Tag** the built commit as `v<version>` and push the tag.
6. **Upload a draft release** with the three files: `gh release create v<version> --draft --verify-tag --notes-file <notes>`. The release notes are the matching section of `CHANGELOG.md`.
7. **Start the tests:** `gh workflow run release-test.yml -f tag=v<version> -f beta=<true|false>`.
8. **Follow the run** in the terminal (`gh run watch`) and print the result and the link to the release or to the failed run. Closing the terminal does not stop the run.

The script never publishes anything itself; only the workflow does, after the tests pass.

Other commands of the same script:

- `scripts/release --discard v<version>` deletes the draft and its tag (local and on GitHub). It refuses to touch a release that is already published.
- `scripts/release --retest v<version>` starts the test workflow again for an existing draft.

### Remote test workflow

`.github/workflows/release-test.yml` starts only by hand from the Actions tab or from the release script (`workflow_dispatch` with inputs `tag` and `beta`). It never runs on push or pull request. It tests each build on a clean GitHub-hosted Mac of the matching architecture:

| Job | Runner | Tests |
|---|---|---|
| `test-arm64` | `macos-15` (Apple Silicon) | `Brouk-<version>-macos-arm64.zip` |
| `test-x64` | `macos-15-intel` | `Brouk-<version>-macos-x64.zip` |

Both runners use macOS 15, the minimum version Brouk supports (002).

Each test job:

1. Checks out the repository **at the tag**, only for the test scripts and test pages.
2. Downloads its zip and `SHA256SUMS.txt` from the draft (`gh release download`) and verifies the checksum.
3. Unzips the app and checks that its signature is intact (`codesign --verify --deep --strict`). From phase 2 also that it is notarized and accepted by Gatekeeper (`spctl --assess`, `stapler validate`).
4. Runs the **smoke tests** (`tests/smoke/`). Brouk is started with a new, empty temporary data folder and with flags that avoid system prompts a test machine cannot answer (for example the keychain prompt; the exact flags are checked against CEF, see `CLAUDE.md`):
   - `brouk --version` prints the version and commit hash of the tag.
   - The app starts and is still running after 10 seconds.
   - It loads the local test pages (served on localhost from the repository) and each page reports its expected title. The test reads this over the Chrome DevTools Protocol, so the test starts Brouk with `--remote-debugging-port` (006; localhost only).
   - A page that crashes its renderer shows the crash page (012), and the app keeps running.
   - It saves a screenshot of every test page.
   - It quits cleanly within 10 seconds, and no crash report of Brouk appears.
5. Uploads the screenshots and logs as run artifacts, kept for 14 days, whether the tests pass or fail.

Rules:

- Each job times out after 20 minutes.
- Only one run per tag at a time (`concurrency` group per tag).
- The workflow uses only its own `GITHUB_TOKEN` with `contents: write`; no other secrets.
- Third-party actions are pinned to exact versions, as in `linux.yml`.

### Publishing

A `publish` job runs only when **all** test jobs pass. It turns the same draft into a visible release, without touching its files:

- with `beta=true`: `gh release edit v<version> --draft=false --prerelease`
- with `beta=false`: `gh release edit v<version> --draft=false --latest`

The run summary shows the link to the published release.

### When a test fails

- Nothing is published, and the draft stays in place.
- The failed run shows which check failed, with screenshots and logs attached.
- A failure caused by the runner rather than Brouk can be retried with "Re-run failed jobs" in Actions, or with `scripts/release --retest`, using the same draft.
- For a real bug the user runs `scripts/release --discard v<version>`, fixes the bug, commits and runs `scripts/release` again.
- GitHub emails the user about failed runs (default notification setting), so a failure is not missed after closing the terminal.

### Private repository phase

Everything above works the same on the private repository. Two things differ:

- **Minutes.** The free plan includes 2,000 minutes a month for private repositories, and macOS minutes count 10 times (001 Notes). Two test jobs of about 8 minutes each cost about 160 minutes, so roughly 12 releases a month fit. Smoke tests are kept short in this phase. Once the repository with the workflow is public, standard runners are free.
- **Beta testers.** Each colleague needs a GitHub account and **Read** access to the repository (Settings → Collaborators). Read access lets them download published releases and open issues, but not push. They never see drafts. Until phase 2 signing, they must allow Brouk once in System Settings → Privacy & Security → "Open Anyway"; the release notes say how.

### Before making the repository public

- [ ] Scan the whole git history for secrets (for example with `gitleaks`), not only the current files.
- [ ] Add a licence file.
- [ ] Protect `main`: no force-push, no deletion.
- [ ] Decide whether outside people may open issues and pull requests, or whether feedback goes only through the in-app form (018).
- [ ] Check that release notes, test pages and specs contain no client names or internal URLs.

## Acceptance criteria

- [ ] `scripts/release` refuses to run when a precondition fails (not on `main`, uncommitted changes, unpushed commits, existing tag, `gh` not signed in, no changelog section) and says which one.
- [ ] `scripts/release` builds, signs and packages both architectures, uploads them with `SHA256SUMS.txt` as a draft release tagged `v<APP_VERSION>`, and starts the test workflow.
- [ ] A user without write access to the repository cannot see or download a draft.
- [ ] `release-test.yml` runs only by `workflow_dispatch`; it tests the arm64 build on an Apple Silicon runner and the x64 build on an Intel runner.
- [ ] A test job fails when the checksum or signature check fails, and when any smoke test fails.
- [ ] Screenshots and logs are attached to every test run, passed or failed.
- [ ] When all test jobs pass, the same draft is published (as a pre-release with `beta=true`, as latest otherwise), and the published files are byte-identical to the tested ones (checksums match).
- [ ] When any test job fails, nothing is published, the draft stays, and it can be tested again or discarded with `scripts/release --retest` / `--discard`.
- [ ] `scripts/release --discard` refuses to delete a published release.
- [ ] `brouk --version` and About show the version and the short commit hash.
- [ ] The workflow uses no secrets other than its own `GITHUB_TOKEN`.

## Out of scope

- **Building in Actions.** Builds stay local in phase 1. CI builds, CEF bumps and notarization come with phase 2 (001, "Update process"); they can reuse the test and publish jobs of this spec.
- **Automatic updates** inside Brouk (Sparkle, 001). The update feed will be fed from these releases later.
- **Windows** builds and tests (after Windows support, 002).
- A full end-to-end test suite; only smoke tests. Playwright tests can replace the hand-written DevTools Protocol checks later without changing the pipeline.

## Configuration

None in the app. The release script and workflow live in the repository (`scripts/release`, `.github/workflows/release-test.yml`, `tests/smoke/`).

## Open questions

1. **Smoke test pages:** reuse pages from `docs/testing/pages/` (for example `crash-and-hang.html`) or keep a small separate set in `tests/smoke/pages/`?
2. **Changelog:** keep `CHANGELOG.md` by hand, or have Claude Code write the version's section from the commits (Conventional Commits) when preparing a release, for the user to review?
3. **Where beta releases live:** releases of this source repository (testers need Read access and see the code), or a separate releases repository (001, "Update process" point 4), which would also serve the update feed later?
4. **Intel after fall 2027:** GitHub retires its Intel macOS runners with the macOS 15 image in fall 2027. Then test the x64 build under Rosetta on an Apple Silicon runner, or on the user's Intel MacBook as a self-hosted runner?

## Notes

- 2026-10-25 (proposal, from discussion with the user): local builds, remote tests in GitHub Actions on draft releases, publish only after the tests pass; start on the private repository and switch to public after colleagues have beta tested. GitHub Actions is free on standard runners for public repositories; private repositories get 2,000 minutes a month on the free plan, with macOS counted 10 times ([GitHub docs](https://docs.github.com/en/billing/concepts/product-billing/github-actions)).
- GitHub's Intel label for standard runners is `macos-15-intel`; Intel macOS support on Actions ends when the macOS 15 image is retired in fall 2027 ([GitHub changelog](https://github.blog/changelog/2025-09-19-github-actions-macos-13-runner-image-is-closing-down/)).
- Implementation hints:
  - Drafts are visible to the workflow's `GITHUB_TOKEN` with `contents: write`, so `gh release download` works on a draft.
  - A tag pushed by the script does not start any workflow; `release-test.yml` has no `push` trigger, so a release is only ever tested when the script or the user starts it.
  - Fresh macOS runners can show first-run prompts; the smoke test runner fails fast with a screenshot instead of waiting for the job timeout.
