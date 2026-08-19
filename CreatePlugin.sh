#!/bin/sh
# CreatePlugin — create a plugin in the engine's Extension/ (the catalog).

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
exec python3 "$SCRIPT_DIR/Tools/create_plugin_ui.py" "$SCRIPT_DIR/Extension" "$@"
