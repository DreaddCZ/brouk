# 019 – Release pipeline and automatic updates

- **Status:** ready
- **Priority:** P2
- **Depends on:** 001, 002, 004 (settings), 006 (remote debugging flag), 012 (crash page), 014 (quit question for running downloads), 015 (session restore)

## Goal

One command on the user's Intel Mac builds a release of Brouk for both Mac architectures, signs and notarizes it, smoke-tests it, and publishes it. Installed copies of Brouk then update themselves, like Chrome does. **The files that were tested are exactly the files that are published**, and nothing is published unless the tests pass.

Everything runs on the user's Mac: no GitHub Actions minutes are used. Builds are published in a separate **public releases repository**, so beta testers need no GitHub account and the updater can download from it, while the source repository stays private.

Scope now: macOS, Apple Silicon and Intel (002). Windows joins later.

## Description

### Versions and channels

- The version stays in **one place**: `APP_VERSION` in `CMakeLists.txt` (see `CLAUDE.md`, Decisions). The release script sets it; the git tag is `v<APP_VERSION>`, for example `v0.4.0`.
- The app shows the version together with the short commit hash, for example `0.4.0 (a1b2c3d)`, in About and in `brouk --version`, so every report (018) points to an exact build.
- A release is either **beta** (`--beta`) or **stable**. Each installed Brouk follows one channel (Settings → Updates): stable gets stable releases only; beta gets both, whichever is newer.
- A published version is never replaced or re-uploaded. A fix after publishing gets a new version number. A version whose tests failed was never published, so its number can be used again.

### Release script

`scripts/release <version> [--beta]` runs on the user's Intel Mac and does everything. It stops with a clear message at the first failed step, and leaves nothing half-published: until step 7 nothing leaves the Mac.

1. **Check preconditions**:
   - on `main`, with no uncommitted changes, and `main` pushed to GitHub;
   - `<version>` is higher than the last published version, and tag `v<version>` does not exist yet;
   - the `gh` and `claude` CLIs are signed in, and `gh` can write to the releases repository;
   - the keychain holds the Developer ID certificate, the notarization credentials (`brouk-notary`, see `docs/building.md`) and the update signing key (see "Update signing key").
2. **Set the version and write the changelog**, without asking the user:
   - The script sets `APP_VERSION` and runs Claude Code non-interactively (`claude -p`) on the commits since the previous release tag (Conventional Commits).
   - Claude Code writes the version's section of `CHANGELOG.md` in plain language for testers, under New, Changed and Fixed. Internal `build`, `ci`, `chore`, `test` and `refactor` commits are left out, and so are client names and internal URLs, because the notes are public.
   - If Claude Code fails, the script falls back to a plain list of the commit summaries grouped by type, so a release never waits for a person.
   - Both changes are committed as `chore(release): v<version>`. The commit is not pushed yet.
3. **Build** the Release configuration for both architectures. The Apple Silicon build is cross-compiled, as decided in 001. The core tests run on the Intel build.
4. **Sign, notarize and package** both builds as `scripts/package_mac` does (Developer ID, hardened runtime, notarized and stapled app and disk image). Each architecture gets:
   - `Brouk-<version>-arm64.dmg` / `Brouk-<version>-x86_64.dmg`, the disk image, used for the first installation and by the updater;
   - `SHA256SUMS.txt`, the checksums of both images.
5. **Check both builds:**
   - each image's checksum, its signature (`codesign --verify --deep --strict`), notarization and Gatekeeper (`stapler validate`, `spctl --assess`);
   - that each app contains only its own architecture (`lipo`);
   - that each app's update feed address matches its architecture.
6. **Smoke-test the Intel build** on this Mac (see "Smoke tests"). The Apple Silicon build is not run; its checks in step 5 are all it gets. This is enough while the only users are beta testers (user's decision); see Notes.
7. **Publish**, only when steps 1–6 passed:
   1. Push the release commit to `main`. Tag it as `v<version>` and push the tag.
   2. Create release `v<version>` in the releases repository with both images, `SHA256SUMS.txt` and the version's changelog section as notes. A beta release is marked as a pre-release.
   3. Add the version to the update feed of each architecture (see "Update feed") and push the feed. The feed is changed last, so it never points to a file that is not uploaded yet.
8. **Print** the link to the release and where the test logs are.

Other forms of the same command:

- `scripts/release <version> --no-publish` does steps 1–6 only, without committing: a trial run that leaves the built and tested images in `build/release/<version>/`.
- If step 7 fails part-way (for example the network drops), running the same command again continues from the first unfinished part of step 7 with the already tested files. It does not rebuild.

### Smoke tests

The smoke tests (`tests/smoke/`) start the Intel build copied out of its disk image. Brouk runs with a new, empty temporary data folder (`BROUK_DEV_DATA_DIR`), the mock keychain (`--use-mock-keychain`) and remote debugging on localhost (006), so it never touches the user's own Brouk, data or keychain, and nothing needs a click.

- `brouk --version` prints the version and the commit hash of the release commit.
- The app starts and is still running after 10 seconds.
- It loads the manual test pages from `docs/testing/pages/` (served on localhost), and each page reports its expected title. The test reads the titles over the Chrome DevTools Protocol.
- `crash-and-hang.html` crashes its renderer. Brouk shows the crash page (012) and keeps running.
- The test saves a screenshot of every test page.
- Brouk quits cleanly within 10 seconds, and no new crash report of Brouk appears in `~/Library/Logs/DiagnosticReports/`.
- The updater is built in and points to the Intel feed.
- The whole run takes at most 5 minutes; a test that hangs fails with a screenshot instead of waiting.

Brouk's windows appear on the user's screen during the test. The user's own Brouk may keep running: the test only controls the copy it started.

Logs and screenshots are kept in `build/release/<version>/smoke/`, whether the tests pass or fail.

### Releases repository

A public GitHub repository, `DreaddCZ/brouk-releases`, holds only what users download:

- **GitHub Releases:** the images, `SHA256SUMS.txt` and release notes of every published version.
- **Update feeds:** `appcast-arm64.xml` and `appcast-x86_64.xml`, served with GitHub Pages at `https://dreaddcz.github.io/brouk-releases/`.
- **A README:** what Brouk is, and links to download the latest stable and beta version for each architecture.

It holds no source code. Setting it up is a one-time step, done once the user approves it. It is created public, and Pages serves its `main` branch. `docs/releasing.md` describes the setup.

The source repository stays private. Beta testers download the disk image from the releases page; they need no GitHub account.

### Update feed

Brouk uses **Sparkle 2** (the standard updater of macOS apps outside the App Store) to check for, download and install updates.

- There is one feed per architecture. Sparkle has no per-architecture filter, so each build knows its own feed address, which is set at build time from one place in `CMakeLists.txt`.
- Each feed item gives the version, the minimum macOS version (15), the image's address and size, its EdDSA signature, and a link to the release notes on GitHub. A beta release is marked as Sparkle's `beta` channel.
- Sparkle installs an update only when its EdDSA signature matches the public key in the app, **and** the new app is signed by the same Apple team.
- Feeds keep every published version, so the history stays visible. Sparkle picks the newest version that the user's channel allows.

### Update signing key

- The script creates the EdDSA key pair once with Sparkle's `generate_keys`, and keeps the private key in the login keychain of the Intel Mac.
- The public key is part of the app (Info.plist), set from one place in `CMakeLists.txt`.
- The script tells the user to back up the private key once (`generate_keys -x`) into their password manager. Without the key, installed copies cannot be updated, and every user would have to install a new image by hand.

### Updating in the app

Brouk follows Chrome: it updates in the background, and the update takes effect at the next restart.

- **When it checks:** about 1 minute after startup, then every 5 hours while Brouk runs, and at any time from **Brouk → Check for Updates…** or Settings → Updates.
- **Downloading:** when a newer version is found for the user's channel, Brouk downloads it in the background. Nothing interrupts the user.
- **Installing:**
  - When Brouk next quits, the update is installed. At the next start the user's windows and tabs come back as after any restart (015).
  - Until then, **Brouk → Restart to Update** and a small note in Settings → Updates say that an update is ready.
  - Restarting asks first only when Chrome would ask when quitting: running downloads (014).
- **Checking by hand** ("Check for Updates…") shows the result in a small window: "Brouk is up to date (0.4.0)", or the new version with its release notes and "Restart to Update" once it is downloaded.
- **Errors:** an automatic check that fails (offline, server error) is retried at the next check without bothering the user; Settings → Updates shows the last error. A check started by hand shows its error in the window.
- **Channel changes:** going from beta to stable never installs an older version. The user stays on their version until a newer stable one is out, as in Chrome.
- **Privacy:** the update check sends only the request for the feed, with Brouk's version in the User-Agent. Sparkle's system profile is turned off.

Settings → Updates shows:

- the version and commit;
- the channel and how updates are installed (see Configuration);
- when Brouk last checked, with the result;
- a "Check now" button.

Only builds made by the release script contain the updater's feed address and key. Local builds (`scripts/build`) and `scripts/package_mac` images never update themselves, so a development build is never replaced by a release. Their Settings → Updates says "Updates are off in this build".

## Acceptance criteria

**Release script**

- [ ] `scripts/release` refuses to run when a precondition fails, and says which one:
  - not on `main`, uncommitted changes or unpushed commits;
  - a version not higher than the last published one, or an existing tag;
  - `gh` or `claude` not signed in, or no access to the releases repository;
  - no Developer ID certificate, notarization credentials or update signing key.
- [ ] `scripts/release` sets `APP_VERSION`, writes the version's `CHANGELOG.md` section without asking anything, and commits both. Without Claude Code it falls back to a grouped list of commits.
- [ ] Both architectures are built, signed with Developer ID, notarized and stapled. Their checksum, signature, notarization and architecture checks pass, or the release stops.
- [ ] The smoke tests run on the Intel build with a temporary data folder and the mock keychain. They leave the user's running Brouk, data and keychain untouched, and keep screenshots and logs whether they pass or fail.
- [ ] When any check or smoke test fails, nothing is pushed, tagged or published.
- [ ] When all pass, the release appears in the releases repository with both images, `SHA256SUMS.txt` and the changelog section as notes, as a pre-release with `--beta`. The tag `v<version>` is in the source repository, and both feeds list the version.
- [ ] The published images are byte-identical to the tested ones (checksums match).
- [ ] `--no-publish` builds and tests without committing or publishing anything.
- [ ] Running the script again after an interrupted step 7 finishes publishing without rebuilding.
- [ ] `brouk --version` and About show the version and the short commit hash.

**Updates**

- [ ] An installed Brouk of the previous version, on the stable channel, finds a newer stable release within a minute of starting, downloads it, and runs the new version after the next restart. Its windows and tabs are restored.
- [ ] On the stable channel, a beta release is not offered; on the beta channel it is.
- [ ] An update whose EdDSA signature does not match, or that is signed by another team, is not installed.
- [ ] Brouk → Check for Updates… reports "up to date", a found update, or an error.
- [ ] "Restart to Update" appears once an update is downloaded, and restarts into the new version.
- [ ] With `updates.mode: notify`, an update is announced but downloaded and installed only when the user chooses. With `off`, Brouk does not check by itself.
- [ ] Switching from beta to stable does not install an older version.
- [ ] Builds from `scripts/build` and `scripts/package_mac` never check for updates.

## Out of scope

- **Building or testing in GitHub Actions** (macOS minutes). The Linux core CI (001) stays as it is.
- **Running the Apple Silicon build in the release tests.** Revisit when there are real users beyond beta testers (see Notes).
- **Automatic CEF updates** (001, "Update process" point 1): picking up new CEF releases stays a manual pull request for now. The pipeline releases whatever `main` contains.
- **Delta updates** (downloading only the changed part). Sparkle supports them; add them when full downloads become a problem.
- **Windows** builds and updates (after Windows support, 002).
- A full end-to-end test suite; only smoke tests. Playwright tests can replace the hand-written DevTools Protocol checks later without changing the pipeline.

## Configuration

Global settings (004), in Settings → Updates:

| Setting | Values | Default | Meaning |
|---|---|---|---|
| `updates.channel` | `stable`, `beta` | `stable` | Which releases this Brouk installs. Beta testers switch to `beta`. |
| `updates.mode` | `install`, `notify`, `off` | `install` | `install`: download in the background and install at the next restart. `notify`: say that an update is available; download and install only when the user chooses. `off`: never check by itself; checking by hand still works. |

## Open questions

None.

## Notes

- 2026-10-25 (first version, from discussion with the user): local builds, remote tests in GitHub Actions on draft releases, publish only after the tests pass; start on the private repository and switch to public after colleagues have beta tested.
- 2026-10-26 (user's decisions, spec rewritten):
  - **GitHub minutes:** the free minutes ran out and the account's spending limit stops jobs. So the smoke tests run locally instead of on GitHub-hosted Macs.
  - **One Mac:** releases are built on the user's Intel Mac only, and only the Intel build is run in the tests. The user cannot always reach the M2 Mac and cannot give remote access to it. The Apple Silicon build gets the signature, notarization and architecture checks but is not started. This is accepted while the only users are beta testers.
  - **Publishing:** releases go to a separate public releases repository, which decides the update feed hosting left open in 001 ("Update process", point 4).
  - **Updater:** Sparkle, with automatic updates included in this spec, and a stable/beta channel setting.
  - **Superseded:** draft releases, the GitHub test workflow, `--retest` and `--discard`.
- 2026-10-26: Developer ID signing and notarization already exist in `scripts/package_mac` (PR #47, 001 Notes); the release script reuses them.
- Chrome's behaviour followed:
  - updates download in the background and apply at the next restart;
  - "Relaunch to update" appears once one is ready;
  - checks run every few hours;
  - switching to a more stable channel does not downgrade.
- Implementation hints:
  - **Sparkle as a dependency:** fetched as the prebuilt release archive at an exact version with a checksum (like `scripts/fetch_cef`), and listed in `third_party/README.md` (MIT). The archive also holds `generate_keys`, `sign_update` and `generate_appcast`. Brouk is not sandboxed, so Sparkle's XPC services are not needed.
  - **Signing Sparkle:** its nested helpers (`Autoupdate`, `Updater.app`) must be re-signed inside out with Developer ID and the hardened runtime before the app is signed.
  - **Channels:** `SPUUpdaterDelegate allowedChannelsForUpdater:` returns `{"beta"}` on the beta channel.
  - **Feed address:** `SUFeedURL` and `SUPublicEDKey` go into Info.plist only for release-script builds.
  - **Check interval:** `SUScheduledCheckInterval` is 5 hours. `SUAutomaticallyUpdate` follows `updates.mode`. `SUEnableSystemProfiling` is NO.
  - **Version numbers:** `CFBundleVersion` must grow with each release for Sparkle's version comparison; derive it from `APP_VERSION`.
  - **Gatekeeper:** an update from Sparkle is not quarantined, but it is notarized and stapled anyway.
- If Apple Silicon testing is needed later without the M2, GitHub's free macOS runners for public repositories could test builds from the public releases repository.
