#!/bin/sh
# CreateProject — new-project UI (Maho project wizard).
# Entry contract: cd to this script's folder, run the tools with the ENGINE-local
# Python only (Tools/maho_pythonw.sh -> Tools/maho_python.sh -> Tools/python),
# forward all args to Tools/create_project.py. Never a system python3.
set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

exec "$SCRIPT_DIR/Tools/maho_pythonw.sh" "$SCRIPT_DIR/Tools/create_project.py" "$@"
