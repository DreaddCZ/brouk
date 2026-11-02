# Releasing Brouk

How releases are built, tested and published, and how installed copies
update themselves (spec 019). Everything runs on the Intel Mac.

## Making a release

```sh
scripts/release 0.2.0 --beta        # a beta release (pre-release, beta feed channel)
scripts/release 0.2.0               # a stable release
scripts/release 0.2.0 --no-publish  # trial run: build and test only
```

The script:

1. checks the preconditions (on `main`, clean, pushed; the version is new;
   `gh` and `claude` signed in; certificate, notarization credentials and
   update signing key in the keychain);
2. sets `APP_VERSION`, lets Claude Code write the version's section of
   `CHANGELOG.md` (or lists the commits if it cannot) and commits both as
   `chore(release): v<version>`;
3. builds both architectures with updates on, signs them with Developer ID,
   notarizes and staples the apps and the disk images
   (`scripts/package_mac --all --updates`), and runs the core tests;
4. checks each image: checksum, signature, notarization, Gatekeeper, the
   architecture of the app and of Sparkle, the update feed address and the
   bundle version;
5. smoke-tests the Intel build (`tests/smoke/smoke.mjs`): it starts a copy
   with an empty temporary data folder and the mock keychain, loads the test
   pages, crashes a renderer and quits it. Your own Brouk can keep running;
6. publishes: pushes the commit and the tag `v<version>`, creates the release
   in [DreaddCZ/brouk-releases](https://github.com/DreaddCZ/brouk-releases)
   with both images and `SHA256SUMS.txt`, then adds the version to both
   update feeds.

If anything fails before publishing, nothing has left the Mac and the release
commit is undone; fix the problem and run the same command again. If
publishing is interrupted (e.g. the network dropped), run the same command
again: it continues with the tested images and does not rebuild.

Everything a release produced is in `build/release/<version>/`: the images,
`SHA256SUMS.txt`, the changelog section, and the smoke tests' log and
screenshots in `smoke/`.

A published version is never replaced. A fix needs a new version number.

## Windows

```sh
scripts/release 0.3.0 --beta     # as above: macOS, and pushes the tag v0.3.0
scripts/release_windows 0.3.0    # then the Windows installer of that tag
```

Pushing the tag starts `windows-release.yml` on GitHub: a Release build of
the tagged commit (no caches), with the core tests, the smoke test and the
installer test (`scripts/test_windows_installer`: installs it quietly,
starts it, uninstalls it). `scripts/release_windows` waits for that run,
downloads its installer, checks the checksum, signs it for the updater with
the same key as the macOS images (Sparkle's `sign_update`), uploads
`Brouk-<version>-setup.exe` and its `.sha256` to the release, and adds it to
the Windows feed `windows-x64.json` (beta when the release is a
pre-release). Running it again continues after an interruption.

The installer is **not code-signed** yet, so SmartScreen warns about an
unknown publisher (More info → Run anyway).

On Windows, Brouk's own updater (not Sparkle) reads
`https://dreaddcz.github.io/brouk-releases/windows-x64.json`, checks a minute
after the start and every 5 hours, downloads the newest release of its
channel, checks its Ed25519 signature and runs the installer silently when
Brouk quits (⋮ → Restart to update installs at once). The installer waits
for Brouk to end and keeps the user's data.

## One-time setup

Done already on the Intel Mac; repeat on a new Mac.

- **Developer ID and notarization:** see `docs/building.md`.
- **Releases repository:** `DreaddCZ/brouk-releases`, public, with GitHub
  Pages serving its `main` branch. It holds a README, `.nojekyll` and the
  feeds `appcast-arm64.xml` and `appcast-x86_64.xml`.
- **Update signing key:** an EdDSA key made by Sparkle's `generate_keys`,
  kept in the login keychain under the account `brouk`:

  ```sh
  scripts/fetch_sparkle
  third_party/sparkle/bin/generate_keys --account brouk      # makes it once
  third_party/sparkle/bin/generate_keys --account brouk -x brouk-update-key.txt   # backup
  third_party/sparkle/bin/generate_keys --account brouk -f brouk-update-key.txt   # restore on another Mac
  ```

  Keep the backup in two safe places (e.g. the password manager) and delete
  the file. **Without this key, installed copies can never be updated**; every
  user would have to install a new image by hand. The public key is
  `APP_UPDATE_PUBLIC_KEY` in `CMakeLists.txt`; the release script refuses to
  run if the keychain's key does not match it.

## How updates work

- Only builds made by `scripts/release` contain the feed address and the
  public key (`scripts/build --updates`). Local builds and
  `scripts/package_mac` images never update themselves; their
  Brouk → Check for Updates… says so.
- Each architecture has its own feed, set at build time:
  `https://dreaddcz.github.io/brouk-releases/appcast-<arch>.xml`.
- Brouk checks about a minute after it starts and then every 5 hours,
  downloads in the background, and installs when it quits. Once an update is
  ready, Brouk → Restart to Update installs it at once.
- Sparkle installs an update only when its EdDSA signature matches the key in
  the app and the new app is signed by the same Apple team.
- Settings → Updates: channel (`updates.channel`: stable or beta) and mode
  (`updates.mode`: install, notify or off). Beta releases are marked with
  Sparkle's `beta` channel, so stable installs never see them.
