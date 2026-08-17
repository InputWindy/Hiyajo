# Run via Tools/check_plugins.bat — engine Tools/python only.
"""
Validate engine plugins — report missing headers that would break compilation.

Usage:
  Tools\\check_plugins.bat
  Tools\\check_plugins.bat path\\Game.cproject
"""

from __future__ import annotations

import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

from maho_tools import (  # noqa: E402
	ENGINE_ROOT,
	list_engine_plugins,
	parse_cproject_plugin_overrides,
	read_cproject,
	resolve_engine_directory,
	validate_plugins,
)


def main(argv: list[str]) -> int:
	engine_root = ENGINE_ROOT.resolve()
	enabled = None

	# Optional .cproject arg — validate only that project's enabled plugins.
	for arg in argv[1:]:
		p = Path(arg).expanduser().resolve()
		if p.suffix.lower() == ".cproject" and p.is_file():
			data = read_cproject(p)
			engine_root = resolve_engine_directory(p, data)
			overrides = parse_cproject_plugin_overrides(data)
			if overrides is None:
				enabled = {pl["Name"] for pl in list_engine_plugins(engine_root) if pl.get("EnabledByDefault", True)}
			else:
				enabled = {name for name, on in overrides.items() if on}
			break

	problems = validate_plugins(engine_root, enabled=enabled)
	if not problems:
		print("[Maho] All plugins healthy.")
		return 0

	errors = sum(1 for sev, _ in problems if sev == "error")
	for sev, msg in problems:
		tag = "ERROR" if sev == "error" else "WARN"
		print(f"[{tag}] {msg}")
	print(f"[Maho] {len(problems)} problem(s), {errors} error(s).")
	print("[Maho] Fix: run fix_plugins.bat (batch) or double-click a broken .cplugin.")
	return 1 if errors else 0


if __name__ == "__main__":
	raise SystemExit(main(sys.argv))
