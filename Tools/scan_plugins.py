# Run via Tools/scan_plugins.bat / maho_python.bat — engine Tools/python only.
"""
Scan .cplugin manifests and resolve module dependency order for compilation.

Usage:
  Tools\\scan_plugins.bat
  Tools\\scan_plugins.bat --cproject path\\Game.cproject
  Tools\\scan_plugins.bat --out Intermediate\\plugin_modules.json
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

from maho_tools import (  # noqa: E402
	DEFAULT_ENGINE_PLUGINS_DIR,
	ENGINE_ROOT,
	parse_cproject_plugin_overrides,
	read_cproject,
	resolve_plugin_roots_for_cproject,
	scan_plugin_modules,
)


def _parse_args(argv: list[str]) -> argparse.Namespace:
	parser = argparse.ArgumentParser(
		description="Scan Maho .cplugin files and emit a module build-order JSON.",
	)
	parser.add_argument(
		"--engine-root",
		type=Path,
		default=ENGINE_ROOT,
		help="Engine repository root (default: parent of Tools/)",
	)
	parser.add_argument(
		"--plugins-dir",
		type=Path,
		action="append",
		default=None,
		help="Extra / override plugin root (repeatable). Default: <engine>/Maho/Plugins",
	)
	parser.add_argument(
		"--cproject",
		type=Path,
		default=None,
		help="Game .cproject — also scan that project's Plugins/ folder",
	)
	parser.add_argument(
		"--out",
		type=Path,
		default=None,
		help="Write scan result JSON (UTF-8). Prints to stdout if omitted.",
	)
	parser.add_argument(
		"--include-disabled",
		action="store_true",
		help="Include plugins with EnabledByDefault=false",
	)
	parser.add_argument(
		"--check",
		action="store_true",
		help="Validate only (exit 0/1); still writes --out when set",
	)
	return parser.parse_args(argv[1:])


def main(argv: list[str]) -> int:
	args = _parse_args(argv)
	engine_root = args.engine_root.resolve()

	try:
		enabled_overrides = None
		if args.cproject is not None:
			cproject = args.cproject.expanduser().resolve()
			if cproject.suffix.lower() != ".cproject":
				print(f"[ERROR] Expected a .cproject file, got: {cproject}", file=sys.stderr)
				return 1
			if not cproject.is_file():
				print(f"[ERROR] File not found: {cproject}", file=sys.stderr)
				return 1
			roots = resolve_plugin_roots_for_cproject(cproject)
			enabled_overrides = parse_cproject_plugin_overrides(read_cproject(cproject))
		elif args.plugins_dir:
			roots = [p.expanduser().resolve() for p in args.plugins_dir]
		else:
			roots = [(engine_root / "Maho" / "Plugins").resolve()]
			if not roots[0].is_dir():
				roots = [DEFAULT_ENGINE_PLUGINS_DIR.resolve()]

		result = scan_plugin_modules(
			roots,
			include_disabled=bool(args.include_disabled),
			enabled_overrides=enabled_overrides,
		)
	except Exception as ex:  # noqa: BLE001
		print(f"[ERROR] {ex}", file=sys.stderr)
		return 1

	text = json.dumps(result, indent=2, ensure_ascii=False) + "\n"

	if args.out is not None:
		out_path = args.out.expanduser().resolve()
		out_path.parent.mkdir(parents=True, exist_ok=True)
		out_path.write_text(text, encoding="utf-8", newline="\n")
		print(f"[Maho] Wrote {out_path}")
	else:
		sys.stdout.write(text)

	print(
		f"[Maho] Plugins={len(result['Plugins'])} Modules={len(result['Modules'])} "
		f"BuildOrder={', '.join(result['BuildOrder']) or '(none)'}"
	)

	if args.check:
		print("[Maho] plugin scan OK")
	return 0


if __name__ == "__main__":
	raise SystemExit(main(sys.argv))
