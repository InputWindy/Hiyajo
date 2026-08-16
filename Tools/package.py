# Run via Tools/maho_python.bat / package.bat — engine Tools/python only.
"""
Build Release and install into Packaged/<Platform>/.

Usage:
  Tools\\package.bat
  Tools\\package.bat path\\Game.cproject
  Tools\\package.bat path\\Game.cproject Debug
"""

from __future__ import annotations

import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

from maho_tools import (  # noqa: E402
	ENGINE_ROOT,
	generate_engine_workspace,
	generate_from_cproject,
	run_package,
)

_CONFIGS = {
	"debug": "Debug",
	"release": "Release",
	"relwithdebinfo": "RelWithDebInfo",
	"minsizerel": "MinSizeRel",
}


def main(argv: list[str]) -> int:
	config = "Release"
	platform = "Win64"
	cproject: Path | None = None

	for arg in argv[1:]:
		key = arg.lower()
		if key in _CONFIGS:
			config = _CONFIGS[key]
		elif key.startswith("win") or key in {"linux", "mac"}:
			platform = "Win64" if key.startswith("win") else arg
		else:
			cproject = Path(arg).expanduser().resolve()

	try:
		if cproject is not None:
			if cproject.suffix.lower() != ".cproject":
				print(f"[ERROR] Expected .cproject, got: {cproject}")
				return 1
			generate_from_cproject(cproject)
			project_dir = cproject.parent
			label = cproject.stem
		else:
			generate_engine_workspace(ENGINE_ROOT)
			project_dir = ENGINE_ROOT
			label = "MahoWorkspace"

		run_package(project_dir, config=config, platform=platform)
		print(f"[Maho] Package finished for {label} ({config} / {platform})")
		return 0
	except Exception as ex:  # noqa: BLE001
		print(f"[ERROR] {ex}")
		return 1


if __name__ == "__main__":
	raise SystemExit(main(sys.argv))
