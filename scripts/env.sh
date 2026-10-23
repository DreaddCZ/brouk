# Source this file to use the pinned tools from .tools/ (see scripts/install_tools):
#   source scripts/env.sh
_brouk_root="$(cd "$(dirname "${BASH_SOURCE[0]:-${(%):-%x}}")/.." && pwd)"
export PATH="$_brouk_root/.tools/bin:$PATH"
# Cargo keeps its downloads inside the repository's tools, not in ~/.cargo.
export CARGO_HOME="$_brouk_root/.tools/cargo"
unset _brouk_root
