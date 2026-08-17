#!/usr/bin/env sh
# New-plugin UI (create_plugin_ui.py) — engine Tools/python only.
set -u
cd "$(dirname "$0")/.." || exit 1
exec "$(dirname "$0")/maho_python.sh" "$(dirname "$0")/create_plugin_ui.py" "$@"
