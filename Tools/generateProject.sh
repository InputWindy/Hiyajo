#!/bin/sh
# Internal: generate build files from .cproject.
# Also the target of the Linux .cproject file association (double-click .cproject).
# Console tool: output must stay visible — run it from a terminal (the .desktop
# entry does not open one), so it never hides its own result.
set -u

TOOLS_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$TOOLS_DIR" || exit 1

exec "$TOOLS_DIR/maho_python.sh" "$TOOLS_DIR/generateProject.py" "$@"
