# Tools/scan_deps.py — scan MAHO_EXTEND_DEPS globally and emit a JSON class
# dependency table.
#
# The main problem with the old scan_closure.py is that it regex-scans raw text
# and gets fooled by comments/string literals (a doc comment mentioning
# MAHO_EXTEND_DEPS(...) gets registered as a real dependency; braces inside
# comments break class-range detection). This version strips comments and string
# literals FIRST (replacing them with spaces, preserving brace/paren depth),
# then reliably locates classes and their MAHO_EXTEND_DEPS calls.
#
# Usage (engine-local python only):
#   maho_python.bat Tools\\scan_deps.py --src Source --src Plugins --out deps.json
#
# Output JSON:
#   {
#     "FWorld":  { "deps": ["FAI", "FGameClock"], "file": "Path.h" },
#     "FAI":     { "deps": [],                      "file": "Path.h" },
#     ...
#   }
#
# "deps" are the raw (Key, Parent, Extras...) flattened to their extras + no
# dedup — the human-readable anchor table. Consumers (TTypeList generation,
# cycle checks) refine it later.

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

_DEPS_CALL = re.compile(r"\bMAHO_EXTEND_DEPS\s*\(")
# a bare identifier that's a plausible class/type name
_CLASS_HEAD = re.compile(r"\b(?:class|struct)\s+([A-Za-z_]\w*)\s*(?=[<:{ ])")
_IDENT = re.compile(r"[A-Za-z_]\w*")


def _strip_comments_strings(text: str) -> str:
	"""Replace comments + string/char literals with blanks (== same length) so
	line/column offsets and brace/paren depth are preserved."""
	out = list(text)
	i = 0
	n = len(text)
	while i < n:
		c = text[i]
		# line comment
		if c == "/" and i + 1 < n and text[i + 1] == "/":
			j = text.find("\n", i)
			j = n if j == -1 else j
			for k in range(i, j):
				out[k] = " "
			i = j
			continue
		# block comment
		if c == "/" and i + 1 < n and text[i + 1] == "*":
			j = text.find("*/", i + 2)
			j = n if j == -1 else j + 2
			for k in range(i, j):
				out[k] = " "
			i = j
			continue
		# char literal 'x' / '"' string — skip to closing (handle escapes crudely)
		if c in "\"'":
			quote = c
			j = i + 1
			while j < n:
				if text[j] == "\\":
					j += 2
					continue
				if text[j] == quote:
					j += 1
					break
				j += 1
			for k in range(i, j):
				out[k] = " "
			i = j
			continue
		i += 1
	return "".join(out)


def _find_balanced(clean: str, start: int, open_char: str, close_char: str) -> int:
	"""For text[start] == open_char (the OPENING brace/paren), return the index of
	the mapping close_char (exclusive end). -1 on unbalance."""
	depth = 0
	i = start
	n = len(clean)
	while i < n:
		c = clean[i]
		if c == open_char:
			depth += 1
		elif c == close_char:
			depth -= 1
			if depth == 0:
				return i
		i += 1
	return -1


def _split_groups(inner: str) -> list[str]:
	"""Split on commas at paren-depth 0; keep each group's wrapper parens."""
	groups: list[str] = []
	depth = 0
	cur: list[str] = []
	for ch in inner:
		if ch == "(":
			depth += 1
		elif ch == ")":
			depth -= 1
		if ch == "," and depth == 0:
			groups.append("".join(cur).strip())
			cur = []
		else:
			cur.append(ch)
	if cur:
		groups.append("".join(cur).strip())
	return [g for g in groups if g]


def _iter_deps_calls(clean: str):
	"""Yield (group_tuples, call_end) for every MAHO_EXTEND_DEPS call in clean text."""
	for m in _DEPS_CALL.finditer(clean):
		paren = clean.find("(", m.start())
		if paren == -1:
			continue
		end = _find_balanced(clean, paren + 1, "(", ")")
		if end == -1:
			continue
		inner = clean[paren + 1 : end]
		yield _split_groups(inner), end + 1


def _parse_group(group: str) -> tuple[str, str, list[str]] | None:
	"""'(Key, Parent, Extras...)' → (Key, Parent, [extras])."""
	group = group.strip()
	if not (group.startswith("(") and group.endswith(")")):
		return None
	inner = group[1:-1]
	parts = [p.strip() for p in _split_groups(inner)]
	if len(parts) < 2:
		return None
	key, parent = parts[0], parts[1]
	extras = [p for p in parts[2:] if p]
	return (key, parent, extras)


def _scan_file(path: Path) -> dict[str, dict]:
	raw = path.read_text(encoding="utf-8", errors="replace")
	clean = _strip_comments_strings(raw)
	out: dict[str, dict] = {}

	# Interval method: for each class head with a body, locate its `{...}` range
	# once via balanced scan (ranges are disjoint so total cost is linear), then
	# attribute every MAHO_EXTEND_DEPS call lexically inside that range to the class.
	calls: list[tuple[int, int, list[tuple[str, str, list[str]]]]] = []
	for m in _DEPS_CALL.finditer(clean):
		paren = clean.find("(", m.start())
		if paren == -1:
			continue
		# count from the macro's own '(' so extra()'s inner (group) parens nest
		end = _find_balanced(clean, paren, "(", ")")
		if end == -1:
			continue
		inner = clean[paren + 1 : end]
		groups = [g for g in _split_groups(inner) if g]
		deps: list[tuple[str, str, list[str]]] = []
		for g in groups:
			triple = _parse_group(g)
			if triple:
				deps.append(triple)
		calls.append((m.start(), end, deps))

	for m in _CLASS_HEAD.finditer(clean):
		rest = clean[m.end() :]
		semi = rest.find(";")
		brace = rest.find("{")
		if brace == -1 or (semi != -1 and semi < brace):
			continue  # forward decl / no body
		body_start = m.end() + brace
		body_end = _find_balanced(clean, body_start, "{", "}")
		if body_end == -1:
			continue
		name = m.group(1)
		owned = [(s, e, d) for (s, e, d) in calls
				 if s >= body_start and e <= body_end]
		if not owned:
			continue
		entry = out.setdefault(name, {"deps": [], "file": str(path)})
		for _s, _e, deps in owned:
			entry["deps"].extend(deps)
	return out


def scan_sources(paths: list[Path]) -> dict[str, dict]:
	"Merge per-file scans into one global table (later files overwrite on generic)."
	merged: dict[str, dict] = {}
	for p in sorted(str(x) for x in paths):
		path = Path(p)
		result = _scan_file(path)
		for name, info in result.items():
			merged[name] = info  # last wins on name collision
	return merged


def _collect_source_files(roots: list[Path]) -> list[Path]:
	files: list[Path] = []
	for root in roots:
		r = root.resolve()
		if r.is_file():
			files.append(r)
		else:
			files.extend(sorted(r.rglob("*.h")))
			files.extend(sorted(r.rglob("*.cpp")))
	return files


def _emit_gen_h(table: dict[str, dict], out_dir: Path) -> int:
	"""For each class with deps, write a sibling `<File>.gen.h` next to its
	declaring header. Each contains `#define MAHO_DEPS_<Class>_<Key> <deps...>`
	so a consumer writes `using FDepends = TTypeList<Key, TTypeList<MAHO_DEPS_X>>`.

	Groups classes by their declaring file (one .gen.h per file), keyed by
	`<file_stem>.gen.h` into the same directory.
	"""
	per_file: dict[str, list[tuple[str, list[tuple[str, str, list[str]]]]]] = {}
	for cls, info in table.items():
		deps = info.get("deps", [])
		if not deps:
			continue
		f = info.get("file", "")
		per_file.setdefault(f, []).append((cls, deps))

	wrote = 0
	for fpath, classes in per_file.items():
		src = Path(fpath)
		gen = src.with_suffix(".gen.h") if out_dir is None else out_dir / (src.stem + ".gen.h")
		gen.parent.mkdir(parents=True, exist_ok=True)

		lines = ["// Generated by Tools/scan_deps.py — DO NOT EDIT.\n",
				 "// Dependency macros per (Class, Key). Use:\n",
				 "//   using FDepends = TTypeList<FDefaultSlot, TTypeList<MAHO_DEPS_Class_Key>>;\n",
				 "#pragma once\n"]
		for cls, deps in sorted(classes):
			for (key, _parent, extras) in deps:
				value = ", ".join(extras) if extras else ""
				# sanitize macro name (Key and Class are identifiers)
				names = [cls, key]
				if any(not re.fullmatch(r"[A-Za-z_]\w*", n) for n in names):
					continue
				lines.append(f"#define MAHO_DEPS_{cls}_{key} {value}\n")
		gen.write_text("".join(lines), encoding="utf-8", newline="\n")
		wrote += 1
	print(f"[Maho] Wrote {wrote} dependency header(s)")
	return 0


def _parse_args(argv: list[str]) -> argparse.Namespace:
	parser = argparse.ArgumentParser(
		description="Scan MAHO_EXTEND_DEPS globally and emit a JSON dependency table (+ optional .gen.h macros).",
	)
	parser.add_argument("--out", type=Path, required=True, help="Output JSON path")
	parser.add_argument(
		"--src", type=Path, action="append", default=None,
		help="Source file or dir to scan (repeatable; default: engine Source/ + Plugins/)",
	)
	parser.add_argument(
		"--emit-h", type=Path, default=None,
		help="Write per-file <stem>.gen.h dependency macros into this dir (default: next to declaring header)",
	)
	return parser.parse_args(argv[1:])


def main(argv: list[str]) -> int:
	args = _parse_args(argv)
	if args.src:
		roots = args.src
	else:
		from maho_tools import ENGINE_ROOT

		roots = [ENGINE_ROOT / "Source", ENGINE_ROOT / "Plugins"]

	files = _collect_source_files(roots)
	if not files:
		print("[ERROR] no sources to scan", file=sys.stderr)
		return 1

	table = scan_sources(files)
	args.out.parent.mkdir(parents=True, exist_ok=True)
	args.out.write_text(
		json.dumps(table, indent=2, ensure_ascii=False) + "\n",
		encoding="utf-8", newline="\n",
	)
	print(f"[Maho] Wrote {args.out} ({len(table)} classes)")
	if args.emit_h is not None:
		_emit_gen_h(table, args.emit_h)
	else:
		# default: write each class's .gen.h next to its declaring header
		_emit_gen_h(table, None)
	return 0


if __name__ == "__main__":
	raise SystemExit(main(sys.argv))
