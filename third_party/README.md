# Third-party code

Every third-party library and binary distribution used by the build, with its
pinned version and licence. Adding one needs the user's approval (see
`CLAUDE.md`) and is called out in the pull request.

| Name | Version | Licence | How it is fetched | Why |
|---|---|---|---|---|
| Chromium Embedded Framework (CEF), minimal binary distribution | 154.0.34+g14c5a08+chromium-154.0.8037.98 | BSD-3-Clause (CEF); Chromium components under their own licences, listed in the distribution's `CREDITS.html` | `scripts/fetch_cef` (SHA-256 checked) into `third_party/cef/<platform>/` | The browser engine (spec 001) |
| yaml-cpp | 0.8.0 | MIT | CMake `FetchContent` in `cmake/BroukYaml.cmake` (SHA-256 checked) | Reading and writing the YAML settings files (spec 004); approved by the user 2026-10-22. It throws exceptions, so only `src/core/yaml_io.cc` uses it and catches them all |
| SQLite (amalgamation) | 3.54.1 | Public domain | CMake `FetchContent` in `cmake/BroukSqlite.cmake` (SHA-256 checked; the SHA3-256 matches the one published on sqlite.org) | The per-profile history databases (specs 013 and 003); approved by the user 2026-10-23. Used only by the core's history storage |
| adblock-rust (crate `adblock`) | 0.13.3 | MPL-2.0 | Cargo, pinned with every dependency by version and checksum in `src/adblock/Cargo.lock`; built by `cmake/BroukAdblock.cmake` | Brave's ad and tracker blocking engine (spec 007); approved by the user 2026-10-23. Used only through our C wrapper `src/adblock` (the only Rust code) and `src/core/adblock_engine.h`. Its dependencies (serde, regex, flatbuffers, psl, …) are MIT/Apache-2.0 crates listed in the lock file; `psl` (the public suffix list) is also used directly for the password manager's site matching (008) |
| libsodium | 1.0.22 | ISC | CMake `FetchContent` in `cmake/BroukSodium.cmake` (SHA-256 checked; the same file as the GitHub release `1.0.22-RELEASE`) | Encrypting saved passwords and autofill data, and the password-protected profile export (specs 008, 009); approved by the user 2026-10-24. Used only by `src/core/crypto.cc` |
| Sparkle | 2.10.0 | MIT | `scripts/fetch_sparkle` (SHA-256 checked, the same as GitHub's digest of the release asset) into `third_party/sparkle/` | The updater of release builds (spec 019); approved by the user 2026-10-26. `Sparkle.framework` is embedded thinned to the build's architecture and without its XPC services (Brouk is not sandboxed); `generate_keys` and `sign_update` are used by `scripts/release` |
| GoogleTest | 1.18.0 | BSD-3-Clause | CMake `FetchContent` in `tests/CMakeLists.txt` (SHA-256 checked) | C++ unit tests (spec 001) |

Build tools (not linked into the app) are pinned in `scripts/install_tools`:
CMake 4.4.4, Ninja 1.13.2, Node.js 24.21.0 LTS, GitHub CLI 2.102.0,
clang-format and clang-tidy 22.1.8, Rust 1.98.0 (rustc and cargo, only for
the ad blocking engine; specs 007 and 001). The TypeScript compiler (`tsc`, chosen in
spec 001) is pinned in `web/package-lock.json` with its integrity hash:
TypeScript 6.0.3 (Apache-2.0), installed by `scripts/web`. The only package the
pages use at run time is the password strength estimate of the passwords page
(008, approved by the user 2026-10-24): `@zxcvbn-ts/core` 4.2.0,
`@zxcvbn-ts/language-common` 4.1.3 and `@zxcvbn-ts/language-en` 4.1.1 (MIT),
with their dependencies `@zxcvbn-ts/dictionary-compression` 3.0.1 and
`fastest-levenshtein` 1.0.16 (MIT), pinned in the same lockfile and copied
into the page by `scripts/vendor_modules.mjs`.
