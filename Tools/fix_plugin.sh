#!/usr/bin/env sh
# Auto-fix a single plugin's missing generated headers (Api.h + .gen.h).
set -u
cd "$(dirname "$0")/.." || exit 1
exec "$(dirname "$0")/maho_python.sh" "$(dirname "$0")/fix_plugin.py" "$@"
