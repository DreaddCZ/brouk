# Building Brouk

## macOS (phase 1: local builds)

Requirements: macOS 15 or later, Xcode (full Xcode, not only the Command Line
Tools), about 2 GB of free disk space.

One command builds the app:

```sh
scripts/build                 # Debug build for this Mac (Intel or Apple Silicon)
scripts/build --release       # Release build
scripts/build --arch arm64    # Apple Silicon build, also from an Intel Mac
scripts/build --arch x86_64   # Intel build, also from an Apple Silicon Mac
scripts/build --test          # also run the tests
```

The app bundle is written to `build/<config>-<arch>/Brouk.app`. Start it by
double-clicking it in Finder, or from a terminal (URLs given as arguments open
as tabs):

```sh
open build/debug-x86_64/Brouk.app
build/debug-x86_64/Brouk.app/Contents/MacOS/brouk https://example.com
```

What `scripts/build` does:

1. `scripts/install_tools` downloads the pinned build tools (CMake, Ninja,
   Node.js, GitHub CLI, clang-format, clang-tidy) into `.tools/` and checks
   their checksums. Nothing is installed system-wide. (Homebrew is not used:
   it no longer ships prebuilt tools for Intel Macs.)
2. `scripts/fetch_cef` downloads the pinned CEF version for the target
   architecture into `third_party/cef/<platform>/`, checksum-verified. The
   archive is cached in `~/.cache/brouk/cef/`.
3. CMake + Ninja build the app into `build/<config>-<arch>/`.

To use the pinned tools directly in a terminal: `source scripts/env.sh`.

### Build types and checks

- **Debug** builds use UndefinedBehaviorSanitizer in the app, and
  AddressSanitizer + UndefinedBehaviorSanitizer in the tests. (The app process
  cannot run under AddressSanitizer: the CEF framework traps at startup.)
- Compiler warnings are errors.
- `scripts/format` formats the code (`--check` only reports), `scripts/lint
  [build-dir]` runs clang-tidy on the portable code.

### Signing certificate (once per Mac)

```sh
scripts/create_dev_certificate
```

creates a self-signed "Brouk Local Dev" code signing certificate in your login
keychain (macOS asks for your password to trust it). Builds are then signed
with it, so macOS recognises every new build as the same app: keychain access
and camera/microphone permissions are remembered across rebuilds. Without it,
builds are signed ad-hoc and macOS asks again after every rebuild.

### Keychain prompt on first start

Chromium keeps the key that encrypts cookies and saved logins in the macOS
keychain ("Chromium Safe Storage"). On first start macOS asks for permission;
choose **Always Allow**.

For automated developer test runs only, `BROUK_DEV_MOCK_KEYCHAIN=1` makes
Chromium use a throwaway key instead of the keychain. Do not use it for normal
browsing.

`BROUK_DEV_DATA_DIR=/absolute/path` stores all data in that folder instead of
the one below, so a test instance can run next to your own Brouk (one instance
runs per data folder):

```sh
open -n --env BROUK_DEV_MOCK_KEYCHAIN=1 --env BROUK_DEV_DATA_DIR=/tmp/brouk-test \
  build/debug-x86_64/Brouk.app --args https://example.com/
```

### Data

All data is stored in `~/Library/Application Support/Brouk/`:

| Folder | Content |
|---|---|
| `profiles/<id>/` | our own per-profile files (settings, later) |
| `web/` | Chromium's data root |
| `web/<id>/` | a profile's web data: cookies, cache, local storage |

Until profiles (spec 005) exist there is one profile with the id `default`.
Deleting the `Brouk` folder resets the browser completely.

## Linux (core and tests only)

The portable code (`src/core/`, `src/cef/`) builds and is tested on Linux;
this is what CI runs (`.github/workflows/linux.yml`). Same scripts:

```sh
scripts/install_tools && source scripts/env.sh
scripts/fetch_cef linux64
cmake -S . -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug && ctest --test-dir build/debug
```

Without CEF (core only): add `-DBROUK_WITH_CEF=OFF`.

## Updating pinned versions

- Tools: versions and SHA-256 checksums at the top of `scripts/install_tools`.
- CEF: `CEF_VERSION` and checksums in `scripts/fetch_cef` (the comment there
  explains how to verify a new download).
- GoogleTest: `tests/CMakeLists.txt`, and `third_party/README.md`.
