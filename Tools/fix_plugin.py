#!/usr/bin/env python3
# Run via Tools/fix_plugin.bat (double-click .cplugin) — engine Tools/python only;
# never a system Python (no MAHO_ALLOW_SYSTEM_PYTHON escape hatch here).
"""Validate + auto-fix one plugin on .cplugin double-click."""

import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

from maho_tools import check_and_fix_plugin  # noqa: E402


def main(argv: list[str]) -> int:
	if len(argv) < 2:
		print("[ERROR] usage: fix_plugin.py <Plugin.cplugin>", file=sys.stderr)
		return 1
	target = Path(argv[1]).expanduser().resolve()
	if target.suffix.lower() != ".cplugin" or not target.is_file():
		print(f"[ERROR] Expected a .cplugin file, got: {target}", file=sys.stderr)
		return 1
	return check_and_fix_plugin(target)


if __name__ == "__main__":
	raise SystemExit(main(sys.argv))
