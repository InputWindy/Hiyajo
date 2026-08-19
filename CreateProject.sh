#!/bin/sh
# CreateProject — Maho project UI.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
exec python3 "$SCRIPT_DIR/Tools/create_project.py" "$@"
