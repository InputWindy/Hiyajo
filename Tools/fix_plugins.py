# Run via Tools/fix_plugins.bat — engine Tools/python only.
"""
Batch-fix all engine plugins: regenerate missing Api.h + .gen.h headers.

Usage:
  Tools\\fix_plugins.bat
"""

from __future__ import annotations

import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

from maho_tools import ENGINE_ROOT, fix_plugins  # noqa: E402


def main(argv: list[str]) -> int:
	engine_root = ENGINE_ROOT.resolve()
	for arg in argv[1:]:
		p = Path(arg).expanduser().resolve()
		if p.is_dir():
			engine_root = p

	messages = fix_plugins(engine_root)
	for m in messages:
		print(f"[Maho] {m}")

	if any(m.startswith("UNFIXABLE") for m in messages):
		print("[Maho] Some headers cannot be auto-generated — see UNFIXABLE above.")
		return 1
	print(f"[Maho] Fixed {len(messages)} item(s).")
	return 0


if __name__ == "__main__":
	raise SystemExit(main(sys.argv))
