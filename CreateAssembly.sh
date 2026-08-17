#!/usr/bin/env sh
# Launch the new-assembly UI (CreateAssembly). On Linux there is no console
# hiding (Windows uses WScript + pythonw), so run create_assembly_ui.py directly.
set -u
exec "$(dirname "$0")/Tools/maho_python.sh" "$(dirname "$0")/Tools/create_assembly_ui.py" "$@"
