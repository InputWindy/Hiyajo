#!/bin/sh
# CreatePlugin — new-plugin UI.
# Entry contract: cd to this script's folder, run the tools with the ENGINE-local
# Python only (Tools/maho_pythonw.sh -> Tools/maho_python.sh -> Tools/python),
# forward all args to Tools/create_plugin_ui.py. Never a system python3.
# No default directory is passed: the UI script itself defaults to
# <cwd>/Plugins (= <engine>/Plugins here) for project plugins and
# <engine>/Plugins for engine plugins.
set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

exec "$SCRIPT_DIR/Tools/maho_pythonw.sh" "$SCRIPT_DIR/Tools/create_plugin_ui.py" "$@"
