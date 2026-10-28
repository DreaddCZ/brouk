# Updating CEF

Brouk is built on a pinned CEF version (`CEF_VERSION` in `scripts/fetch_cef`).
CEF follows Chromium, whose security fixes only reach us when we update, so
updates follow a fixed policy (specs/001, "Update process"; agreed with the
user on 2026-10-28):

| Update | Example | Take it within | Usual work |
|---|---|---|---|
| **Patch** (same CEF major) | 154.0.33 → 154.0.34 | **7 days** after the first newer patch | version and checksums only |
| **Major** (new Chromium) | 154 → 156 | **28 days** (one Chromium cycle); never more than one major behind | fix API changes, full manual check |

Every CEF update is followed by a Brouk release (`docs/releasing.md`), so the
fixes reach installed copies through the updater.

## How we notice

`scripts/check_cef` compares the pinned version with CEF's build index
(`cef-builds.spotifycdn.com/index.json`) and prints what is newer. It fails
when an update is overdue by the policy above. It runs in `scripts/sast`, so
the weekly `security` workflow fails (and GitHub sends an e-mail) when an
update is overdue; run it by hand at any time. The rules live in
`scripts/cef_versions.mjs` (tested by `scripts/cef_versions.test.mjs`).

## How to update

1. Branch: `chore/cef-<version>` from `main`.
2. `scripts/update_cef [version]` (default: the newest stable one). It
   downloads the minimal distribution for macOS Intel, macOS Apple Silicon,
   Linux and Windows (CI), checks each against the SHA-1 in CEF's index,
   writes the new version and SHA-256 checksums into `scripts/fetch_cef` and
   `third_party/README.md`, fetches the new CEF for this Mac, and writes the changes of CEF's API
   headers to `build/cef-include.diff`.
3. Major updates: review `build/cef-include.diff` — the headers are the
   source of truth for the API (CLAUDE.md), not memory. Fix compile errors,
   and check the doc comments of every callback and function we use for
   changed behaviour (threads, ownership, defaults). Record notable changes
   in the pull request.
4. Build and test both Mac architectures and Linux: `scripts/build --test`,
   `scripts/build --arch arm64` (cross build; tests run on that Mac),
   `scripts/lint`, `scripts/sast`. CI builds Linux.
5. Run the manual checklist `docs/testing/cef-update.md` on the user's Mac
   (patch updates: steps marked "patch"; major updates: all).
6. Pull request with the Chromium version, the API changes and the
   checklist; after merge, a release.
