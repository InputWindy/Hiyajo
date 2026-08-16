#!/usr/bin/env sh
# Internal: generate build files from .cproject (or the engine workspace).
# Also the target of the Linux .cproject file association (double-click .cproject).
set -u
cd "$(dirname "$0")/.." || exit 1
exec "$(dirname "$0")/maho_python.sh" "$(dirname "$0")/generateProject.py" "$@"
