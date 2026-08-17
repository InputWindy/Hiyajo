# Run via Tools/fix_plugins.bat — engine Tools/python only.
"""
Batch-fix plugins: regenerate missing Api.h + starter .md/.html docs.

Usage:
  Tools\\fix_plugins.bat
  Tools\\fix_plugins.bat path\\Plugins   (scan a specific plugins dir, e.g. a project's)
"""

from __future__ import annotations

import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

from maho_tools import ENGINE_ROOT, fix_plugins  # noqa: E402


def main(argv: list[str]) -> int:
	engine_root = ENGINE_ROOT.resolve()
	roots: list[Path] = []
	for arg in argv[1:]:
		p = Path(arg).expanduser().resolve()
		if p.is_dir():
			roots.append(p)

	messages = fix_plugins(engine_root, plugin_roots=roots or None)
	for m in messages:
		print(f"[Maho] {m}")

	if any(m.startswith("UNFIXABLE") for m in messages):
		print("[Maho] Some headers cannot be auto-generated — see UNFIXABLE above.")
		return 1
	print(f"[Maho] Fixed {len(messages)} item(s).")
	return 0


if __name__ == "__main__":
	raise SystemExit(main(sys.argv))
