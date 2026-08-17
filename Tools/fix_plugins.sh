#!/usr/bin/env sh
# Batch-fix all engine plugins: regenerate missing Api.h + .gen.h headers.
set -u
cd "$(dirname "$0")/.." || exit 1
exec "$(dirname "$0")/maho_python.sh" "$(dirname "$0")/fix_plugins.py" "$@"
