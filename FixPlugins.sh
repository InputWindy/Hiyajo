#!/usr/bin/env sh
# Batch-fix every engine plugin — equivalent to double-clicking each .cplugin:
# regenerate missing Api.h + starter <Name>.md / <Name>.html docs.
set -u
cd "$(dirname "$0")" || exit 1
exec "$(dirname "$0")/Tools/maho_python.sh" "$(dirname "$0")/Tools/fix_plugins.py" "$@"
