#!/usr/bin/env sh
# Launch the CreateProject GUI. On Linux there is no console hiding
# (Windows uses WScript + pythonw for that), so run create_project.py directly.
set -u
exec "$(dirname "$0")/Tools/maho_python.sh" "$(dirname "$0")/Tools/create_project.py" "$@"
