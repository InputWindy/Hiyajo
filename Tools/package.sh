#!/bin/sh
# Launch the packaging GUI with the engine-local Python only
# (Tools/maho_python.sh -> Tools/python; never a system python3).
# On Linux there is no "pythonw" (console hiding is Windows-only), so the GUI
# runs on the normal interpreter — same script, same args.
set -u

TOOLS_DIR="$(cd "$(dirname "$0")" && pwd)"

exec "$TOOLS_DIR/maho_python.sh" "$TOOLS_DIR/package_ui.py" "$@"
