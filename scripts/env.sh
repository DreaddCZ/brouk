# Source this file to use the pinned tools from .tools/ (see scripts/install_tools):
#   source scripts/env.sh
_brouk_root="$(cd "$(dirname "${BASH_SOURCE[0]:-${(%):-%x}}")/.." && pwd)"
export PATH="$_brouk_root/.tools/bin:$PATH"
# On Windows the tools' own directories go on PATH (see scripts/install_tools).
if [[ -f "$_brouk_root/.tools/paths" ]]; then
  while IFS= read -r _brouk_dir; do
    [[ -n "$_brouk_dir" ]] && export PATH="$_brouk_dir:$PATH"
  done < "$_brouk_root/.tools/paths"
  unset _brouk_dir
fi
# Cargo keeps its downloads inside the repository's tools, not in ~/.cargo.
export CARGO_HOME="$_brouk_root/.tools/cargo"
unset _brouk_root
# Build against the SDK of the selected Xcode (xcode-select), whose linker can
# read it. Without this, a Mac whose Command Line Tools are newer than its
# Xcode compiles against the newer SDK, and the older linker fails on it.
if [[ "$(uname -s)" == Darwin && -z "${SDKROOT:-}" ]]; then
  _brouk_sdk="$(xcrun --sdk macosx --show-sdk-path 2>/dev/null || true)"
  [[ -n "$_brouk_sdk" ]] && export SDKROOT="$_brouk_sdk"
  unset _brouk_sdk
fi
