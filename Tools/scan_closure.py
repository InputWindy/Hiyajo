# Run via Tools/maho_python.bat (or Tools/*.bat) — engine Tools/python only.
"""
Scan MAHO_EXTEND_DEPS registrations and emit dependency-closure macros.

The C++ side (Topology.h) reads a pre-computed, deduplicated closure per
(Class, Key) as a generated macro:

  #define MAHO_CLOSURE_0_SA_IA   ::Maho::TTypeList<>
  #define MAHO_CLOSURE_0_SD_IA   ::Maho::TTypeList<SA, SB, SC>

Semantics (the 3D DAG): a class's deps at Key = its parent's deps at Key
(inherited edge, z-axis) union its own direct extras. The closure is the
transitive closure of that, deduplicated, excluding the class itself.

Usage:
  Tools\\scan_closure.py --out Source\\Gen\\Closure.gen.h [--src dir|file ...]
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

from maho_tools import ENGINE_ROOT  # noqa: E402

# ── source scanning ────────────────────────────────────────────────────

# A struct/class declaration with a name (ignore forward decls with no body).
_CLASS_RE = re.compile(r"\b(?:struct|class)\s+([A-Za-z_]\w*)")
_DEPS_CALL = re.compile(r"\bMAHO_EXTEND_DEPS\s*\(")

# Group separator: (Key, Parent, Extras...) — split on commas at paren depth 0
# and strip each group's wrapping parens.
def _split_groups(inner: str) -> list[str]:
	groups: list[str] = []
	depth = 0
	cur: list[str] = []
	for ch in inner:
		if ch == "(":
			depth += 1
		elif ch == ")":
			depth -= 1
		if ch == "," and depth == 0:
			groups.append("".join(cur))
			cur = []
		else:
			cur.append(ch)
	if cur:
		groups.append("".join(cur))

	cleaned: list[str] = []
	for g in groups:
		g = g.strip()
		# Strip the wrapping ( ... ) — groups are spelled (Key, Parent, ...).
		if g.startswith("(") and g.endswith(")"):
			g = g[1:-1].strip()
		if g:
			cleaned.append(g)
	return cleaned


def _find_class_ranges(text: str) -> list[tuple[str, int, int]]:
	"""[(name, start, end)] for every struct/class with a braced body."""
	ranges: list[tuple[str, int, int]] = []
	for m in _CLASS_RE.finditer(text):
		name = m.group(1)
		brace = text.find("{", m.end())
		if brace == -1:
			continue
		depth = 1
		i = brace + 1
		while i < len(text) and depth > 0:
			if text[i] == "{":
				depth += 1
			elif text[i] == "}":
				depth -= 1
			i += 1
		ranges.append((name, m.start(), i))
	return ranges


def _parse_deps_call(text: str, start: int) -> tuple[list[str], int] | None:
	"""Parse MAHO_EXTEND_DEPS(...) at start; return ([group-tuples], end)."""
	paren = text.find("(", start)
	if paren == -1:
		return None
	depth = 0
	i = paren
	while i < len(text):
		if text[i] == "(":
			depth += 1
		elif text[i] == ")":
			depth -= 1
			if depth == 0:
				inner = text[paren + 1 : i]
				return (_split_groups(inner), i + 1)
		i += 1
	return None


def _parse_group(group: str) -> tuple[str, str, list[str]] | None:
	"""'(Key, Parent, Extras...)' → (Key, Parent, [extras])."""
	parts = [p.strip() for p in group.split(",")]
	if len(parts) < 2:
		return None
	key, parent = parts[0], parts[1]
	extras = [p for p in parts[2:] if p]
	return (key, parent, extras)


def scan_sources(paths: list[Path]) -> dict[str, list[tuple[str, str, list[str]]]]:
	"""Class name → [(Key, Parent, extras), ...] from every MAHO_EXTEND_DEPS."""
	registrations: dict[str, list[tuple[str, str, list[str]]]] = {}
	for path in paths:
		text = path.read_text(encoding="utf-8")
		for name, start, end in _find_class_ranges(text):
			body = text[start:end]
			pos = 0
			while True:
				m = _DEPS_CALL.search(body, pos)
				if not m:
					break
				parsed = _parse_deps_call(body, m.start())
				if parsed is None:
					break
				groups, after = parsed
				for g in groups:
					triple = _parse_group(g)
					if triple:
						registrations.setdefault(name, []).append(triple)
				pos = after
	return registrations


# ── closure computation ────────────────────────────────────────────────

_NO_PARENT = "FNoParent"


def _build_deps(registrations: dict[str, list[tuple[str, str, list[str]]]]) -> dict[tuple[str, str], set[str]]:
	"""(Class, Key) → set(direct deps). Fixed point: parent's deps inherited."""
	deps: dict[tuple[str, str], set[str]] = defaultdict(set)
	for cls, groups in registrations.items():
		for key, parent, extras in groups:
			deps[(cls, key)].update(extras)

	# Inherit the parent's edges until stable (3D DAG z-axis).
	changed = True
	while changed:
		changed = False
		for (cls, key), _ in list(deps.items()):
			for _, parent, _ in registrations.get(cls, []):
				if parent == _NO_PARENT:
					continue
				inherited = deps.get((parent, key), set())
				if not inherited <= deps[(cls, key)]:
					deps[(cls, key)] |= inherited
					changed = True
	return dict(deps)


def _compute_closures(registrations: dict[str, list[tuple[str, str, list[str]]]]) -> dict[tuple[str, str], list[str]]:
	"""(Class, Key) → ordered transitive closure (excludes the class itself)."""
	deps = _build_deps(registrations)
	closures: dict[tuple[str, str], list[str]] = {}
	for (cls, key), _ in sorted(deps.items()):
		seen: list[str] = []
		visited: set[str] = set()
		stack = list(deps[(cls, key)])
		while stack:
			t = stack.pop()
			if t in visited or t == cls:
				continue
			visited.add(t)
			seen.append(t)
			stack.extend(deps.get((t, key), ()))
		closures[(cls, key)] = seen
	return closures


# ── emission ───────────────────────────────────────────────────────────

_HEADER = """// Generated by Tools/scan_closure.py — DO NOT EDIT.
// Dependency closures per (Class, Key), deduplicated and acyclic.
#pragma once
"""


def _macro_name(class_name: str, key: str) -> str:
	return f"MAHO_CLOSURE_0_{class_name}_{key}"


def _macro_value(closure: list[str]) -> str:
	inner = ", ".join(closure)
	return f"::Maho::TTypeList<{inner}>"


def emit(closures: dict[tuple[str, str], list[str]]) -> str:
	lines = [_HEADER]
	for (cls, key), closure in sorted(closures.items()):
		lines.append(f"#define {_macro_name(cls, key)} {_macro_value(closure)}")
	return "\n".join(lines) + "\n"


# ── CLI ───────────────────────────────────────────────────────────────

def _collect_source_files(roots: list[Path]) -> list[Path]:
	files: list[Path] = []
	for root in roots:
		if root.is_file():
			files.append(root)
		else:
			for pattern in ("*.h", "*.cpp"):
				files.extend(sorted(root.rglob(pattern)))
	return files


def _parse_args(argv: list[str]) -> argparse.Namespace:
	parser = argparse.ArgumentParser(
		description="Scan MAHO_EXTEND_DEPS and emit dependency-closure macros.",
	)
	parser.add_argument("--out", type=Path, required=True, help="Generated header path (e.g. Source/Gen/Closure.gen.h)")
	parser.add_argument("--src", type=Path, action="append", default=None, help="Source file or dir to scan (default: engine Source/)")
	parser.add_argument("--engine-root", type=Path, default=ENGINE_ROOT, help="Engine root (default: parent of Tools/)")
	return parser.parse_args(argv[1:])


def main(argv: list[str]) -> int:
	args = _parse_args(argv)
	roots = args.src or [args.engine_root / "Source"]
	files = _collect_source_files([r.resolve() for r in roots])
	if not files:
		print("[ERROR] no sources to scan", file=sys.stderr)
		return 1

	registrations = scan_sources(files)
	closures = _compute_closures(registrations)
	out = args.out.resolve()
	out.parent.mkdir(parents=True, exist_ok=True)
	out.write_text(emit(closures), encoding="utf-8", newline="\n")
	print(f"[Maho] Wrote {out} ({len(closures)} closures, {len(registrations)} classes)")
	return 0


if __name__ == "__main__":
	raise SystemExit(main(sys.argv))
