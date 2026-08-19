#!/usr/bin/env sh
# Bootstrap Maho local Python (venv + symlink). Rename-safe layout:
#   ${XDG_DATA_HOME:-~/.local/share}/Maho/python/tooling/   real files (outside the repo)
#   <engine>/Tools/python/                                   symlink -> tooling
#
# Usage:
#   Setup.sh           install / repair Tools/python symlink
#   Setup.sh --force   wipe and recreate
set -u
cd "$(dirname "$0")" || exit 1

ROOT="$(pwd)"
PY_LINK="$ROOT/Tools/python"
PY_HOME="${XDG_DATA_HOME:-$HOME/.local/share}/Maho/python/tooling"
PY_EXE="$PY_HOME/bin/python3"

if [ "${1:-}" = "--force" ]; then
	rm -rf "$PY_HOME"
	rm -f "$PY_LINK"
	echo "[Maho] Wiped local Python."
fi

# 1) Reuse the existing venv when present.
if [ -x "$PY_EXE" ]; then
	echo "[Maho] Local Python already present: $PY_EXE"
else
	# 2) Create a venv from system python3.
	if ! command -v python3 >/dev/null 2>&1; then
		echo "[ERROR] python3 not found. Install Python 3.10+ first."
		exit 1
	fi
	python3 -c "import tkinter" 2>/dev/null || {
		echo "[WARN] system python3 has no tkinter; the CreateProject UI needs python3-tk (Debian/Ubuntu)."
	}
	echo "[Maho] Creating venv: $PY_HOME"
	mkdir -p "$(dirname "$PY_HOME")" || exit 1
	python3 -m venv "$PY_HOME" || {
		echo "[ERROR] venv creation failed. Install python3-venv (Debian/Ubuntu)."
		exit 1
	}
fi

# 3) (Re)create the symlink.
if [ ! -L "$PY_LINK" ] || [ ! -x "$PY_LINK/bin/python3" ]; then
	rm -f "$PY_LINK"
	ln -s "$PY_HOME" "$PY_LINK"
	echo "[Maho] Linked Tools/python -> $PY_HOME"
fi

echo "[Maho] Setup finished successfully."
