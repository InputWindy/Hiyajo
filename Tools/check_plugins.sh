#!/usr/bin/env sh
# Validate engine plugins — report missing headers that would break compilation.
set -u
cd "$(dirname "$0")/.." || exit 1
exec "$(dirname "$0")/maho_python.sh" "$(dirname "$0")/check_plugins.py" "$@"
