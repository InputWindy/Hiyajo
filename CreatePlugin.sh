#!/usr/bin/env sh
# Launch the new-plugin UI. On Linux there is no console hiding
# (Windows uses WScript + pythonw for that), so run create_plugin_ui.py directly.
set -u
exec "$(dirname "$0")/Tools/maho_python.sh" "$(dirname "$0")/Tools/create_plugin_ui.py" "$@"
