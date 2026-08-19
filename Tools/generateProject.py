# Run via Tools/generateProject.bat / maho_python.bat — engine Tools/python only.
"""
Generate Visual Studio solution for a Maho project.

Usage:
  Tools\\generateProject.bat
  Tools\\generateProject.bat path\\Game.cproject
"""

from __future__ import annotations

import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

from maho_tools import (  # noqa: E402
	generate_from_cproject,
)


def main(argv: list[str]) -> int:
	try:
		if len(argv) >= 2:
			target = Path(argv[1]).expanduser().resolve()
			if target.suffix.lower() != ".cproject":
				print(f"[ERROR] Expected a .cproject file, got: {target}")
				return 1
			if not target.is_file():
				print(f"[ERROR] File not found: {target}")
				return 1
			sln = generate_from_cproject(target)
		else:
			print("[ERROR] A .cproject is required (the engine is a catalog, no workspace build).")
			return 1

		print("[Maho] Done. Open the .sln in Visual Studio:")
		print(f"        {sln}")
		return 0
	except Exception as ex:  # noqa: BLE001
		print(f"[ERROR] {ex}")
		return 1


if __name__ == "__main__":
	raise SystemExit(main(sys.argv))
