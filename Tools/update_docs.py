#!/usr/bin/env python3
# Run via launch_update_docs.vbs — regenerate docs for a project's Source/.
"""Update project docs: generate <Name>Doc.md + <Name>API.html for the project Source/."""

import os
import sys
from pathlib import Path

os.environ.setdefault("MAHO_ALLOW_SYSTEM_PYTHON", "1")

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

from maho_tools import read_cproject, resolve_engine_directory, update_docs  # noqa: E402


def main(argv: list[str]) -> int:
	if len(argv) < 2:
		print("[ERROR] usage: update_docs.py <Project.cproject>", file=sys.stderr)
		return 1

	cproject = Path(argv[1]).expanduser().resolve()
	if cproject.suffix.lower() != ".cproject" or not cproject.is_file():
		print(f"[ERROR] Expected a .cproject file, got: {cproject}", file=sys.stderr)
		return 1

	data = read_cproject(cproject)
	engine_root = resolve_engine_directory(cproject, data)
	project_dir = cproject.parent

	print(f"[Maho] Project: {data.get('ProjectName', cproject.stem)}")
	print(f"[Maho] Engine:  {engine_root}")

	# Docs for the project's Source/ (own code) + the engine's Source/ (core).
	for target in (project_dir / "Source", engine_root / "Source"):
		if target.is_dir():
			print(f"[Maho] Doc dir: {target}")
			update_docs(target)
	print("[Maho] Docs updated.")
	return 0


if __name__ == "__main__":
	raise SystemExit(main(sys.argv))
