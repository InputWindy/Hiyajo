# Run via launch_fix_plugin.vbs (double-click .cplugin) — engine Tools/python only.
"""
Auto-fix a single plugin's missing generated headers (Api.h + .gen.h).

Usage:
  Tools\\maho_python.bat Tools\\fix_plugin.py path\\Plugin.cplugin
"""

from __future__ import annotations

import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

from maho_tools import fix_plugin  # noqa: E402


def main(argv: list[str]) -> int:
	if len(argv) < 2:
		print("[ERROR] Usage: fix_plugin.py path\\Plugin.cplugin", file=sys.stderr)
		return 1
	target = Path(argv[1]).expanduser().resolve()
	if target.suffix.lower() != ".cplugin" or not target.is_file():
		print(f"[ERROR] Expected a .cplugin file, got: {target}", file=sys.stderr)
		return 1

	messages = fix_plugin(target)
	for m in messages:
		print(f"[Maho] {m}")

	if any(m.startswith("UNFIXABLE") for m in messages):
		print("[Maho] Some headers cannot be auto-generated.")
		return 1
	print("[Maho] Plugin fixed.")
	return 0


if __name__ == "__main__":
	raise SystemExit(main(sys.argv))
