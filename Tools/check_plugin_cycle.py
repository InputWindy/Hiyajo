# Run before a build (via the MahoCheckCycle CMake target) — not the
# engine-python-only entry scripts. Reuses maho_tools without the local-Python
# gate so a plain system python can drive it from inside Visual Studio.
"""Check a Maho project's plugin dependency graph for cycles.

Usage:
  python check_plugin_cycle.py path\\Game.cproject

Exit 0 = OK, 1 = cycle (or malformed project).
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

os.environ["MAHO_ALLOW_SYSTEM_PYTHON"] = "1"
sys.path.insert(0, str(Path(__file__).resolve().parent))

import maho_tools as m  # noqa: E402


def main(argv: list[str]) -> int:
	if len(argv) < 2:
		print("[ERROR] Usage: check_plugin_cycle.py path\\Game.cproject", file=sys.stderr)
		return 1
	try:
		cproject = Path(argv[1]).expanduser().resolve()
		data = m.read_cproject(cproject)
		project_name = str(data["ProjectName"])
		engine_root = m.resolve_engine_directory(cproject, data)
		selected = [
			p["Name"]
			for p in data.get("Plugins", [])
			if p.get("Enabled", True)
		]
		m._resolve_plugin_chain(
			engine_root,
			[p for p in selected if p != project_name],
			cproject.parent,
		)
		print("[Maho] Plugin dependency graph OK")
		return 0
	except ValueError as ex:
		print(f"[ERROR] {ex}", file=sys.stderr)
		return 1
	except Exception as ex:  # noqa: BLE001
		print(f"[ERROR] {ex}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	raise SystemExit(main(sys.argv))
