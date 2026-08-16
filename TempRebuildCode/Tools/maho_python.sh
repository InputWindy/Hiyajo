#!/usr/bin/env sh
# Resolve Maho local Python (Tools/python) and run a script. Never uses system Python.
# Usage: maho_python.sh Tools/foo.py [args...]
# Linux venv layout: Tools/python/bin/python3 (Windows: Scripts/python.exe).
set -u

MAHO_TOOLS="$(cd "$(dirname "$0")" && pwd)"

if [ -x "$MAHO_TOOLS/python/bin/python3" ]; then
	MAHO_PYTHON="$MAHO_TOOLS/python/bin/python3"
else
	echo "[ERROR] Local Python not found under Tools/python"
	echo "[ERROR] From the Maho engine root, run Setup.sh first."
	echo "[ERROR] Do not use a system-installed Python for Maho tools."
	exit 1
fi

exec "$MAHO_PYTHON" "$@"
