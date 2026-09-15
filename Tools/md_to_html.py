#!/usr/bin/env python3
# Run via Tools/maho_python.bat — engine Tools/python only.
"""
md_to_html.py — render the repository's markdown docs as self-contained dark-theme
HTML pages.

The .md files stay the single hand-written source: this tool only READS them and
writes a sibling `<name>.html` (with a "generated" banner). No third-party
dependency (stdlib only), no network, no shared assets dir — the CSS is inlined so
a page can be opened from anywhere.

What it supports (the subset the docs actually use): ATX headings, paragraphs,
fenced code blocks, GFM pipe tables (with alignment), nested bullet/ordered lists,
block quotes, horizontal rules, inline code / bold / italic / links, and raw HTML
blocks (e.g. `<a id="...">` anchors) passed through untouched. `.md` links to files
that exist next to the source are rewritten to `.html` so the pages navigate to
each other.

Usage:
  Tools\\python\\python.exe Tools\\md_to_html.py Source\\Public\\Engine
  Tools\\python\\python.exe Tools\\md_to_html.py --include-drafts Source
"""

from __future__ import annotations

import argparse
import html
import re
import sys
from pathlib import Path

# -- markdown -> html -----------------------------------------------------------

CODE_FENCE = re.compile(r"^\s*(```|~~~)\s*([\w+-]*)\s*$")
HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
RULE = re.compile(r"^\s*(-{3,}|\*{3,}|_{3,})\s*$")
BULLET = re.compile(r"^(\s*)([-*+])\s+(.*)$")
ORDERED = re.compile(r"^(\s*)(\d+)[.)]\s+(.*)$")
QUOTE = re.compile(r"^\s*>\s?(.*)$")
TABLE_DELIM = re.compile(r"^:?-{2,}:?$")


def slugify(text: str) -> str:
	"""Stable id from a heading: CJK kept (valid in HTML ids), everything else folded."""
	plain = re.sub(r"`([^`]*)`", r"\1", text)
	plain = re.sub(r"[^\w\u4e00-\u9fff]+", "-", plain, flags=re.UNICODE)
	return plain.strip("-").lower() or "section"


def rewrite_link(url: str, base_dir: Path) -> str:
	"""Point a sibling `.md` link at its generated `.html` (leave everything else alone)."""
	if url.startswith(("http://", "https://", "mailto:", "data:", "#")):
		return url
	path, _, frag = url.partition("#")
	if path.endswith(".md") and (base_dir / path).exists():
		path = path[:-3] + ".html"
	return path + (("#" + frag) if frag else "")


def inline(text: str, base_dir: Path) -> str:
	# Code spans first, stashed so the other rules cannot touch their contents.
	spans: list[str] = []

	def stash(match: re.Match[str]) -> str:
		spans.append(match.group(1))
		return "\x00%d\x00" % (len(spans) - 1)

	text = re.sub(r"`([^`]+)`", stash, text)
	text = html.escape(text, quote=False)
	text = re.sub(
		r"\[([^\]]+)\]\(([^)\s]+)\)",
		lambda m: '<a href="%s">%s</a>' % (html.escape(rewrite_link(m.group(2), base_dir), quote=True), m.group(1)),
		text,
	)
	text = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", text)
	text = re.sub(r"(?<!\*)\*([^*]+)\*(?!\*)", r"<em>\1</em>", text)

	def restore(match: re.Match[str]) -> str:
		return "<code>" + html.escape(spans[int(match.group(1))], quote=False) + "</code>"

	return re.sub("\x00(\\d+)\x00", restore, text)


def split_row(line: str) -> list[str]:
	"""Split a table row on unescaped `|`, ignoring pipes inside code spans."""
	cells: list[str] = []
	current = ""
	in_code = False
	for ch in line.strip():
		if ch == "`":
			in_code = not in_code
			current += ch
		elif ch == "|" and not in_code:
			cells.append(current)
			current = ""
		else:
			current += ch
	cells.append(current)
	if cells and not cells[0].strip():
		cells = cells[1:]
	if cells and not cells[-1].strip():
		cells = cells[:-1]
	return [c.strip() for c in cells]


def alignments(cells: list[str]) -> list[str | None]:
	out: list[str | None] = []
	for cell in cells:
		left = cell.startswith(":")
		right = cell.endswith(":")
		out.append("center" if left and right else "left" if left else "right" if right else None)
	return out


def is_table_delim(line: str) -> bool:
	cells = split_row(line)
	return bool(cells) and all(TABLE_DELIM.match(c) for c in cells)


def is_table_start(lines: list[str], i: int) -> bool:
	return "|" in lines[i] and i + 1 < len(lines) and is_table_delim(lines[i + 1]) and "|" in lines[i + 1]


def render_markdown(text: str, base_dir: Path) -> tuple[str, list[tuple[int, str, str]]]:
	"""Return (body html, toc entries as (level, id, plain text))."""
	lines = text.splitlines()
	out: list[str] = []
	toc: list[tuple[int, str, str]] = []
	used_ids: dict[str, int] = {}
	i = 0

	def unique_id(heading: str) -> str:
		base = slugify(heading)
		used_ids[base] = used_ids.get(base, 0) + 1
		return base if used_ids[base] == 1 else "%s-%d" % (base, used_ids[base])

	while i < len(lines):
		line = lines[i]

		fence = CODE_FENCE.match(line)
		if fence:
			marker, lang = fence.group(1), fence.group(2)
			i += 1
			body: list[str] = []
			while i < len(lines) and not lines[i].strip().startswith(marker):
				body.append(lines[i])
				i += 1
			i += 1
			cls = ' class="language-%s"' % lang if lang else ""
			out.append("<pre><code%s>%s</code></pre>" % (cls, html.escape("\n".join(body), quote=False)))
			continue

		heading = HEADING.match(line)
		if heading:
			level = len(heading.group(1))
			title = heading.group(2)
			hid = unique_id(title)
			out.append('<h%d id="%s">%s</h%d>' % (level, hid, inline(title, base_dir), level))
			if level in (2, 3):
				toc.append((level, hid, re.sub(r"[`*]", "", title)))
			i += 1
			continue

		if RULE.match(line):
			out.append("<hr>")
			i += 1
			continue

		# Raw HTML block (e.g. <a id="..."> anchors): pass through, unmodified.
		if line.lstrip().startswith("<"):
			block: list[str] = []
			while i < len(lines) and lines[i].strip():
				block.append(lines[i])
				i += 1
			out.append("\n".join(block))
			continue

		if is_table_start(lines, i):
			header = split_row(lines[i])
			aligns = alignments(split_row(lines[i + 1]))
			i += 2
			rows: list[list[str]] = []
			while i < len(lines) and "|" in lines[i] and lines[i].strip():
				rows.append(split_row(lines[i]))
				i += 1
			parts = ['<div class="table-wrap"><table><thead><tr>']
			for c, a in zip(header, aligns):
				style = ' style="text-align:%s"' % a if a else ""
				parts.append("<th%s>%s</th>" % (style, inline(c, base_dir)))
			parts.append("</tr></thead><tbody>")
			for row in rows:
				parts.append("<tr>")
				for c, a in zip(row, aligns):
					style = ' style="text-align:%s"' % a if a else ""
					parts.append("<td%s>%s</td>" % (style, inline(c, base_dir)))
				parts.append("</tr>")
			parts.append("</tbody></table></div>")
			out.append("".join(parts))
			continue

		if BULLET.match(line) or ORDERED.match(line):
			block = []
			while i < len(lines) and (BULLET.match(lines[i]) or ORDERED.match(lines[i])):
				block.append(lines[i])
				i += 1
			out.append(render_list(block, base_dir))
			continue

		if QUOTE.match(line):
			block = []
			while i < len(lines) and QUOTE.match(lines[i]):
				block.append(QUOTE.match(lines[i]).group(1))
				i += 1
			out.append("<blockquote>%s</blockquote>" % render_markdown("\n".join(block), base_dir)[0])
			continue

		if not line.strip():
			i += 1
			continue

		block = []
		while i < len(lines) and lines[i].strip() and not (
			HEADING.match(lines[i]) or RULE.match(lines[i]) or CODE_FENCE.match(lines[i])
			or BULLET.match(lines[i]) or ORDERED.match(lines[i]) or QUOTE.match(lines[i])
			or is_table_start(lines, i) or lines[i].lstrip().startswith("<")
		):
			block.append(lines[i])
			i += 1
		out.append("<p>%s</p>" % inline(" ".join(block), base_dir))

	return "\n".join(out), toc


def render_list(block: list[str], base_dir: Path) -> str:
	"""One level of nesting -- every list in these docs is flat or one indented level."""
	html_parts: list[str] = []
	ordered = bool(ORDERED.match(block[0]))
	stack: list[str] = ["ul"]
	html_parts.append("<ul>" if not ordered else "<ol>")
	first = True
	for line in block:
		match = ORDERED.match(line) or BULLET.match(line)
		indent, text = match.group(1), match.group(3)
		nested = len(indent.expandtabs(4)) >= 2
		if first:
			first = False
		elif nested and not stack[-1].endswith("nested"):
			tag = "ul"
			html_parts.append("<%s class=\"nested\">" % tag)
			stack.append(tag + "nested")
		elif not nested and stack[-1].endswith("nested"):
			tag = stack.pop()[:-6]
			html_parts.append("</%s>" % tag)
		html_parts.append("<li>%s</li>" % inline(text, base_dir))
	while stack and stack[-1].endswith("nested"):
		html_parts.append("</%s>" % stack.pop()[:-6])
	html_parts.append("</ul>" if not ordered else "</ol>")
	return "".join(html_parts)


# -- page shell -----------------------------------------------------------------

CSS = """
:root {
  --bg:#0d1117; --panel:#161b22; --panel2:#1c2128; --line:#30363d;
  --fg:#c9d1d9; --muted:#8b949e; --accent:#58a6ff; --accent2:#7ee787;
  --code:#0b0f14; --warn:#d29922;
}
* { box-sizing:border-box; }
html { scroll-behavior:smooth; }
body {
  margin:0; background:var(--bg); color:var(--fg);
  font:15px/1.65 -apple-system,"Segoe UI",Roboto,"Helvetica Neue","PingFang SC","Microsoft YaHei",sans-serif;
}
a { color:var(--accent); text-decoration:none; }
a:hover { text-decoration:underline; }
code, pre, kbd { font-family:ui-monospace,"Cascadia Mono","JetBrains Mono",Consolas,"Courier New",monospace; }
.layout { display:flex; align-items:flex-start; gap:28px; max-width:1400px; margin:0 auto; padding:0 24px; }
nav.side {
  position:sticky; top:0; align-self:flex-start; flex:0 0 268px; max-height:100vh;
  overflow-y:auto; padding:22px 12px 60px; border-right:1px solid var(--line);
}
nav.side h4 { margin:0 0 6px; font-size:12px; letter-spacing:.08em; text-transform:uppercase; color:var(--muted); }
nav.side ul { list-style:none; margin:0 0 18px; padding:0; }
nav.side li { margin:2px 0; }
nav.side a { color:var(--fg); display:block; padding:3px 8px; border-radius:6px; font-size:13.5px; }
nav.side a:hover { background:var(--panel2); text-decoration:none; }
nav.side a.lvl3 { padding-left:20px; color:var(--muted); font-size:13px; }
nav.side a.here { background:var(--panel2); color:var(--accent); }
main { flex:1 1 auto; min-width:0; padding:26px 0 90px; }
header.page { border-bottom:1px solid var(--line); padding-bottom:14px; margin-bottom:22px; }
header.page .crumbs { color:var(--muted); font-size:13px; }
header.page .gen { margin-top:8px; color:var(--warn); font-size:12.5px; }
h1 { font-size:30px; line-height:1.25; margin:18px 0 14px; }
h2 { font-size:22px; margin:34px 0 12px; padding-bottom:7px; border-bottom:1px solid var(--line); }
h3 { font-size:17.5px; margin:26px 0 10px; }
h4 { font-size:15px; margin:20px 0 8px; color:var(--muted); }
p { margin:10px 0; }
hr { border:0; border-top:1px solid var(--line); margin:26px 0; }
ul, ol { padding-left:26px; }
li { margin:4px 0; }
ul.nested { margin:5px 0; }
code { background:var(--panel2); border:1px solid var(--line); border-radius:5px; padding:1px 5px; font-size:13px; }
pre {
  background:var(--code); border:1px solid var(--line); border-radius:8px;
  padding:14px 16px; overflow-x:auto; margin:14px 0;
}
pre code { background:none; border:0; padding:0; font-size:13px; line-height:1.55; }
blockquote {
  margin:14px 0; padding:8px 16px; border-left:3px solid var(--accent);
  background:var(--panel); border-radius:0 6px 6px 0; color:var(--muted);
}
.table-wrap { overflow-x:auto; margin:14px 0; }
table { border-collapse:collapse; width:100%; font-size:14px; }
th, td { border:1px solid var(--line); padding:7px 11px; vertical-align:top; }
th { background:var(--panel2); text-align:left; font-weight:600; }
tbody tr:nth-child(even) { background:rgba(255,255,255,.02); }
footer.page { margin-top:44px; padding-top:14px; border-top:1px solid var(--line); color:var(--muted); font-size:13px; display:flex; gap:16px; flex-wrap:wrap; }
footer.page .spacer { flex:1 1 auto; }
"""

PAGE = """<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<style>{css}</style>
</head>
<body>
<div class="layout">
<nav class="side">
<div class="crumbs">{crumbs}</div>
<h4>本页目录</h4>
{toc}
{siblings}
</nav>
<main>
<header class="page">
<div class="crumbs">{crumbs}</div>
<div class="gen">generated by Tools/md_to_html.py from {source} — edit the .md, not this file</div>
</header>
{body}
<footer class="page">
<div>{nav_prev}</div>
<div class="spacer"></div>
<div>{nav_next}</div>
</footer>
</main>
</div>
</body>
</html>
"""


def toc_html(toc: list[tuple[int, str, str]]) -> str:
	if not toc:
		return "<ul></ul>"
	items = ["<ul>"]
	for level, hid, title in toc:
		items.append('<li><a class="lvl%d" href="#%s">%s</a></li>' % (level, hid, html.escape(title, quote=False)))
	items.append("</ul>")
	return "".join(items)


def siblings_html(current: Path, ordered: list[Path], root: Path) -> str:
	folder = [p for p in ordered if p.parent == current.parent]
	if len(folder) < 2:
		return ""
	items = ["<h4>本目录</h4><ul>"]
	for path in folder:
		here = " here" if path == current else ""
		items.append('<li><a class="%s" href="%s">%s</a></li>' % (here.strip(), path.with_suffix(".html").name, html.escape(path.stem, quote=False)))
	items.append("</ul>")
	return "".join(items)


def crumbs(current: Path, root: Path) -> str:
	parts = current.parent.relative_to(root).parts if current.parent != root else ()
	trail = " / ".join(html.escape(p, quote=False) for p in parts)
	return ("%s / <span>%s</span>" % (trail, html.escape(current.name, quote=False))) if trail else html.escape(current.name, quote=False)


def build_one(md_path: Path, ordered: list[Path], root: Path) -> Path:
	text = md_path.read_text(encoding="utf-8")
	body, toc = render_markdown(text, md_path.parent)
	index = ordered.index(md_path)
	prev_path = ordered[index - 1] if index > 0 else None
	next_path = ordered[index + 1] if index + 1 < len(ordered) else None

	def link(path: Path, label: str) -> str:
		if path is None:
			return "&nbsp;"
		rel = Path(path.with_suffix(".html").name)
		return '<a href="%s">%s %s</a>' % (rel, label, html.escape(path.stem, quote=False))

	html_text = PAGE.format(
		title=html.escape(md_path.stem, quote=False),
		css=CSS,
		crumbs=crumbs(md_path, root),
		toc=toc_html(toc),
		siblings=siblings_html(md_path, ordered, root),
		source=html.escape(md_path.name, quote=False),
		body=body,
		nav_prev=link(prev_path, "&larr;"),
		nav_next=link(next_path, "&rarr;"),
	)
	out_path = md_path.with_suffix(".html")
	out_path.write_text(html_text, encoding="utf-8", newline="\n")
	return out_path


def collect(paths: list[str], include_drafts: bool) -> tuple[list[Path], Path]:
	files: list[Path] = []
	for raw in paths:
		path = Path(raw)
		if path.is_dir():
			files.extend(sorted(p for p in path.rglob("*.md") if p.is_file()))
		elif path.is_file():
			files.append(path)
	if not include_drafts:
		files = [p for p in files if not p.name.endswith(".draft.md")]
	files = sorted(set(files))
	root = Path(files[0]).parent
	for path in files[1:]:
		while root != Path(".") and root not in path.parents:
			root = root.parent
	return files, root


def main(argv: list[str]) -> int:
	parser = argparse.ArgumentParser(description="Render markdown docs as dark-theme HTML.")
	parser.add_argument("paths", nargs="*", default=["Source"], help="files or directories (default: Source)")
	parser.add_argument("--include-drafts", action="store_true", help="also render *.draft.md")
	args = parser.parse_args(argv[1:])

	files, root = collect(args.paths or ["Source"], args.include_drafts)
	if not files:
		print("[md_to_html] nothing to render")
		return 1

	for md_path in files:
		out_path = build_one(md_path, files, root)
		print("[md_to_html] %s -> %s" % (md_path.as_posix(), out_path.as_posix()))
	print("[md_to_html] %d page(s)" % len(files))
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv))
