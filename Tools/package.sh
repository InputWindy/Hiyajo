#!/usr/bin/env sh
# Launch the packaging GUI. On Linux there is no console hiding, so run the
# UI script directly (Windows uses WScript + pythonw for that).
set -u
exec "$(dirname "$0")/maho_python.sh" "$(dirname "$0")/package_ui.py" "$@"
