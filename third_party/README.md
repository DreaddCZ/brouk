# Third-party code

Every third-party library and binary distribution used by the build, with its
pinned version and licence. Adding one needs the user's approval (see
`CLAUDE.md`) and is called out in the pull request.

| Name | Version | Licence | How it is fetched | Why |
|---|---|---|---|---|
| Chromium Embedded Framework (CEF), minimal binary distribution | 154.0.34+g14c5a08+chromium-154.0.8037.98 | BSD-3-Clause (CEF); Chromium components under their own licences, listed in the distribution's `CREDITS.html` | `scripts/fetch_cef` (SHA-256 checked) into `third_party/cef/<platform>/` | The browser engine (spec 001) |
| GoogleTest | 1.18.0 | BSD-3-Clause | CMake `FetchContent` in `tests/CMakeLists.txt` (SHA-256 checked) | C++ unit tests (spec 001) |

Build tools (not linked into the app) are pinned in `scripts/install_tools`:
CMake 4.4.4, Ninja 1.13.2, Node.js 24.21.0 LTS, GitHub CLI 2.102.0,
clang-format and clang-tidy 22.1.8.
