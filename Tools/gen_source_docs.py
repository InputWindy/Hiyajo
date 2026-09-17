#!/usr/bin/env python3
# Run via Tools/maho_python.bat — engine Tools/python only.
"""
gen_source_docs.py — generate Source/Docs.html: the engine source browser.

ONE self-contained page (inline CSS + JS, no network, no assets dir) so it opens
straight from file://. Left = a tree of Source/ (folders -> *.h leaves); right =
the selected header, grouped per CLASS: description, fields, interface
signatures. Dark theme, matching the house style of the other generated reports.

It is a PRESENTATION tool, not a compiler. The parser understands the shape the
engine headers actually use -- a doc comment immediately above each declaration,
one entity per block, tab indentation -- and degrades gracefully: anything it
cannot classify is still shown (as raw source) rather than dropped.

Usage:
  Tools\\python\\python.exe Tools\\gen_source_docs.py                 # Source/ -> Source/Docs.html
  Tools\\python\\python.exe Tools\\gen_source_docs.py --root Source --out Source\\Docs.html
"""

from __future__ import annotations

import argparse
import html
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable

# -- model ----------------------------------------------------------------------


@dataclass
class FEntity:
	"""One declaration: a class/struct/enum/function/macro/alias, with its doc."""
	Kind: str = "raw"          # class | struct | enum | namespace | function | macro | alias | raw
	Name: str = ""
	Doc: list[str] = field(default_factory=list)
	Lines: list[str] = field(default_factory=list)   # the entity's own source (no doc)
	Members: list["FEntity"] = field(default_factory=list)
	Access: list[str] = field(default_factory=list)  # parallel to Members: access at that point
	Template: str = ""


@dataclass
class FHeader:
	"""One .h file: its path (relative to Source/) + parsed entities."""
	Rel: str = ""
	Entities: list[FEntity] = field(default_factory=list)
	Lines: int = 0


# -- lexing helpers -------------------------------------------------------------

COMMENT_OPEN = re.compile(r"^\s*/\*\*?")
KEYWORDS = (
	"alignas", "auto", "bool", "break", "case", "catch", "char", "class", "const", "constexpr",
	"continue", "default", "delete", "do", "double", "else", "enum", "explicit", "export",
	"extern", "false", "final", "float", "for", "friend", "if", "inline", "int", "long",
	"mutable", "namespace", "new", "noexcept", "nullptr", "operator", "override", "private",
	"protected", "public", "return", "short", "signed", "sizeof", "static", "struct", "switch",
	"template", "this", "throw", "true", "try", "typename", "union", "unsigned", "using",
	"virtual", "void", "volatile", "while",
)
KEYWORD_RE = re.compile(r"\b(" + "|".join(KEYWORDS) + r")\b")
COMMENT_RE = re.compile(r"(//[^\n]*|/\*.*?\*/)", re.DOTALL)
STRING_RE = re.compile(r"(\"(?:[^\"\\]|\\.)*\"|'(?:[^'\\]|\\.)*')")


def escape_highlight(Text: str) -> str:
	"""HTML-escape, then a light highlight pass: comments / strings / keywords.

	Order matters and it is not cosmetic: highlighting keywords over text that ALREADY contains
	markup writes spans inside spans (a comment holding `class=` produced a malformed attribute).
	So comments and strings become placeholders first, keywords run on the remaining text only,
	and the placeholders are substituted back with their own (escaped, unhighlighted) content.
	"""
	Out = html.escape(Text, quote=False)
	Saved: list[str] = []

	def Stash(Match: re.Match) -> str:
		Saved.append('<span class="' + ("cm" if Match.group(0).lstrip().startswith(("//", "/*"))
		                               else "st") + '">' + Match.group(0) + "</span>")
		return "\x00" + str(len(Saved) - 1) + "\x00"

	Out = COMMENT_RE.sub(Stash, Out)
	Out = STRING_RE.sub(Stash, Out)
	Out = KEYWORD_RE.sub(lambda M: '<span class="kw">' + M.group(0) + "</span>", Out)
	return re.sub(r"\x00(\d+)\x00", lambda M: Saved[int(M.group(1))], Out)


DOC_OPEN = re.compile(r"^\s*/\*\*?")
DOC_CLOSE = re.compile(r"\*/\s*$")
DOC_LEAD = re.compile(r"^\s*(\*|//)\s?")


def clean_doc(Doc: Iterable[str]) -> list[str]:
	"""Strip comment markers and leading stars from a doc block; trim blank edges."""
	Out: list[str] = []
	for Line in Doc:
		Line = DOC_OPEN.sub("", Line, count=1)
		Line = DOC_CLOSE.sub("", Line)
		Line = DOC_LEAD.sub("", Line)
		Out.append(Line.rstrip())
	while Out and not Out[0]:
		Out.pop(0)
	while Out and not Out[-1]:
		Out.pop()
	return Out


def count_delta(Line: str) -> int:
	"""Brace delta of a line, ignoring comments and string/char literals."""
	Line = COMMENT_RE.sub("", Line)
	Line = STRING_RE.sub("''", Line)
	return Line.count("{") - Line.count("}")


def is_skippable(Line: str) -> bool:
	S = Line.strip()
	return (not S) or S.startswith("#include") or S.startswith("#pragma")


ENTITY_RE = re.compile(
	r"^\s*(?:(template\s*<.*)\s*)?"
	r"(class|struct|enum(?:\s+class)?|namespace|union)\s+([A-Za-z_]\w*)"
)
FUNC_RE = re.compile(r"^\s*(?:\[\[nodiscard\]\]\s*)?(?:static\s+|inline\s+|virtual\s+|explicit\s+|constexpr\s+)*"
                     r"(?:[A-Za-z_][\w:<>,\s\*&]*?)\s+[A-Za-z_]\w*\s*\(")
MACRO_RE = re.compile(r"^\s*#\s*define\s+([A-Za-z_]\w*)")
USING_RE = re.compile(r"^\s*using\s+([A-Za-z_]\w*)\s*=")


def split_entities(Lines: list[str], Depth0: int = 0) -> tuple[list[FEntity], list[str]]:
	"""Split a body into entities at the given nesting depth. Returns (entities, trailing)."""
	Entities: list[FEntity] = []
	PendingDoc: list[str] = []
	Index = 0
	Count = len(Lines)
	while Index < Count:
		Line = Lines[Index]

		if COMMENT_OPEN.match(Line) or (not PendingDoc and Line.strip().startswith("//")):
			Doc: list[str] = []
			if COMMENT_OPEN.match(Line):
				while Index < Count:
					Doc.append(Lines[Index])
					if "*/" in Lines[Index]:
						Index += 1
						break
					Index += 1
			else:
				while Index < Count and Lines[Index].strip().startswith("//"):
					Doc.append(Lines[Index])
					Index += 1
			PendingDoc = Doc
			continue

		if is_skippable(Line):
			Index += 1
			continue

		Match = ENTITY_RE.match(Line)
		Macro = MACRO_RE.match(Line)
		Using = USING_RE.match(Line)
		IsFunc = None if (Match or Macro or Using) else FUNC_RE.match(Line)
		# Anything else that is not a comment/preprocessor line is a DATA MEMBER: it has no
		# '(' to look like a function and no keyword to look like a type. Dropping these was
		# the difference between an empty 字段 section and a populated one.
		IsField = (not (Match or Macro or Using or IsFunc)
		           and bool(Line.strip())
		           and not Line.strip().startswith("#")
		           and not Line.strip().startswith("}"))

		if not (Match or Macro or Using or IsFunc or IsField):
			Index += 1
			continue

		# Collect the entity's own text: until its braces close, or its ';' at this depth.
		Start = Index
		Delta = 0
		SawBrace = False
		while Index < Count:
			Cur = Lines[Index]
			if Macro and Index > Start and not Cur.rstrip().endswith("\\"):
				Index += 1
				break
			Delta += count_delta(Cur)
			Stripped = COMMENT_RE.sub("", Cur).rstrip()
			SawBrace = SawBrace or Stripped.endswith("{")
			if Macro:
				Index += 1
				if not Cur.rstrip().endswith("\\"):
					break
				continue
			Index += 1
			if SawBrace:
				if Delta <= Depth0:
					break
			elif Stripped.endswith(";"):
				break

		Body = Lines[Start:Index]
		Template = ""
		Kind = "raw"
		Name = ""
		if Match:
			Template = (Match.group(1) or "").strip()
			KindWord = Match.group(2).replace(" ", "")
			Kind = {"class": "class", "struct": "struct", "enumclass": "enum", "enum": "enum",
			        "namespace": "namespace", "union": "struct"}.get(KindWord, "raw")
			Name = Match.group(3)
		elif Macro:
			Kind, Name = "macro", Macro.group(1)
		elif Using:
			Kind, Name = "alias", Using.group(1)
		elif IsFunc:
			Kind = "function"
			Name = signature_name(Body)
		elif IsField:
			Kind = "field"
			Name = field_name(Body)

		Entities.append(FEntity(Kind=Kind, Name=Name, Doc=PendingDoc, Lines=Body, Template=Template))
		PendingDoc = []
	return Entities, PendingDoc


def signature_name(Body: list[str]) -> str:
	"""Best-effort name for a function-like entity."""
	Text = " ".join(L.strip() for L in Body[:3])
	M = re.search(r"([A-Za-z_]\w*)\s*\(", Text)
	return M.group(1) if M else ""


def field_name(Body: list[str]) -> str:
	"""Best-effort name for a data member: the identifier before '{' / '=' / ';' / '['."""
	Text = " ".join(L.strip() for L in Body)
	Text = Text.split(";")[0]
	M = re.search(r"([A-Za-z_]\w*)\s*(?:\{|\[|=|$)", Text)
	return M.group(1) if M else Text[:40]


def inner_lines(Body: list[str]) -> list[str]:
	"""The lines between the outermost braces of a declaration block."""
	Inner = Body[:]
	while Inner and "{" not in Inner[0]:
		Inner.pop(0)
	if Inner:
		Inner[0] = Inner[0][Inner[0].index("{") + 1:]
	while Inner and Inner[-1].strip() in ("", "};"):
		Inner.pop()
	if Inner and Inner[-1].strip().endswith("}"):
		Inner[-1] = Inner[-1][: Inner[-1].rindex("}")]
	return Inner


def parse_members(Body: list[str]) -> tuple[list[FEntity], list[str]]:
	"""Members of a class/struct body, plus the access label in effect for each."""
	Inner = inner_lines(Body)

	Access: list[str] = []
	Current = "public"
	Kept: list[str] = []
	for Line in Inner:
		S = Line.strip()
		M = re.match(r"^(public|protected|private)\s*:", S)
		if M:
			Current = M.group(1)
			Access.append(Current)
			Kept.append(Line)
			continue
		Access.append(Current)
		Kept.append(Line)

	Members, _ = split_entities(Kept, Depth0=0)
	# Access label in effect where each member starts (best effort: by line identity).
	for Member in Members:
		Label = "public"
		try:
			Position = Kept.index(Member.Lines[0])
			Label = Access[Position] if Position < len(Access) else "public"
		except ValueError:
			pass
		Member.Access = [Label]
		if Member.Kind in ("class", "struct", "enum"):
			Nested, _ = parse_members(Member.Lines)
			Member.Members = Nested
	return Members, []


def flatten_entities(Entities: list[FEntity], Out: list[FEntity]) -> None:
	"""Hoist what is INSIDE a namespace to the file's top level.

	Without this the whole file collapses into one `namespace Maho` entity: the classes live
	inside its braces, so a non-recursive split sees exactly one entity per header.
	"""
	for Entity in Entities:
		if Entity.Kind == "namespace":
			Inner, _ = split_entities(inner_lines(Entity.Lines), Depth0=0)
			flatten_entities(Inner, Out)
		else:
			Out.append(Entity)


def parse_header(Path: Path, Rel: str) -> FHeader:
	Text = Path.read_text(encoding="utf-8", errors="replace")
	Lines = Text.splitlines()
	Raw, _ = split_entities(Lines, Depth0=0)
	Entities: list[FEntity] = []
	flatten_entities(Raw, Entities)
	for Entity in Entities:
		if Entity.Kind in ("class", "struct", "enum"):
			Members, _ = parse_members(Entity.Lines)
			Entity.Members = Members
	return FHeader(Rel=Rel, Entities=Entities, Lines=len(Lines))


# -- rendering ------------------------------------------------------------------


def render_entity(Entity: FEntity, Anchor: str) -> str:
	Parts: list[str] = []
	KindClass = {"class": "e-class", "struct": "e-struct", "enum": "e-enum", "function": "e-fn",
	             "macro": "e-macro", "alias": "e-alias", "namespace": "e-ns", "raw": "e-raw"}.get(
		Entity.Kind, "e-raw")
	Title = Entity.Name or (Entity.Lines[0].strip() if Entity.Lines else Entity.Kind)
	Parts.append(f'<div class="ent {KindClass}" id="{Anchor}">')
	Parts.append(f'<h3><span class="kind">{Entity.Kind}</span> {html.escape(Title)}</h3>')

	if Entity.Doc:
		Parts.append('<div class="doc">')
		for Line in clean_doc(Entity.Doc):
			Parts.append(f"<p>{escape_highlight(Line) if Line else '&nbsp;'}</p>")
		Parts.append("</div>")

	if Entity.Members:
		# Group members: fields vs interface, per access section.
		Groups: dict[str, list[FEntity]] = {"fields": [], "interface": [], "nested": []}
		for Member in Entity.Members:
			if Member.Kind in ("class", "struct", "enum"):
				Groups["nested"].append(Member)
			elif Member.Kind == "function":
				Groups["interface"].append(Member)
			elif Member.Kind in ("alias", "macro"):
				Groups["nested"].append(Member)
			else:
				Groups["fields"].append(Member)

		Parts.append('<div class="members">')
		if Groups["fields"]:
			Parts.append('<h4>字段</h4>')
			for Member in Groups["fields"]:
				Parts.append(render_member(Member))
		if Groups["interface"]:
			Parts.append('<h4>接口</h4>')
			for Member in Groups["interface"]:
				Parts.append(render_member(Member))
		if Groups["nested"]:
			Parts.append('<h4>嵌套</h4>')
			for Member in Groups["nested"]:
				Parts.append(render_member(Member))
		Parts.append("</div>")
	else:
		Parts.append('<pre class="src">')
		Parts.append(escape_highlight("\n".join(Entity.Lines)))
		Parts.append("</pre>")

	Parts.append("</div>")
	return "\n".join(Parts)


def render_member(Member: FEntity) -> str:
	Access = Member.Access[0] if Member.Access else "public"
	Signature = " ".join(L.strip() for L in Member.Lines)
	Signature = re.sub(r"\s+", " ", Signature).strip()
	if "{" in Signature:
		Signature = Signature[: Signature.index("{")].strip()
	if len(Signature) > 400:
		Signature = Signature[:400] + " …"
	Body = [f'<div class="sig"><span class="acc a-{Access}">{Access}</span> '
	        f"<code>{escape_highlight(Signature)}</code></div>"]
	if Member.Doc:
		Body.append('<div class="doc small">')
		for Line in clean_doc(Member.Doc):
			Body.append(f"<p>{escape_highlight(Line) if Line else '&nbsp;'}</p>")
		Body.append("</div>")
	return "\n".join(Body)


def render_tree(Headers: list[FHeader]) -> str:
	"""Nested <ul> of folders -> .h leaves, built from the relative paths."""
	Tree: dict = {}
	for H in Headers:
		Node = Tree
		Parts = H.Rel.split("/")
		for Part in Parts[:-1]:
			Node = Node.setdefault(Part, {})
		Node.setdefault("__files__", []).append(H)

	def Emit(Node: dict, Prefix: str) -> str:
		Out = ['<ul>']
		for Key in sorted(K for K in Node if K != "__files__"):
			Sub = f"{Prefix}/{Key}" if Prefix else Key
			Files = Node[Key].get("__files__", [])
			Out.append('<li class="dir"><span class="dir-label" data-dir="' + html.escape(Sub) + '">'
			           f'{html.escape(Key)}</span>')
			Out.append(Emit(Node[Key], Sub))
			Out.append("</li>")
		for H in sorted(Node.get("__files__", []), key=lambda X: X.Rel):
			Anchor = H.Rel.replace("/", "__").replace(".", "_")
			Out.append(f'<li class="file"><a href="#{Anchor}" data-file="{Anchor}">'
			           f'{html.escape(Path(H.Rel).name)}</a></li>')
		Out.append("</ul>")
		return "\n".join(Out)

	return Emit(Tree, "")


CSS = """
:root{
  --bg:#0f1420; --panel:#1a2234; --panel2:#182238; --line:#2a3550; --txt:#e6ecf5; --sub:#9fb0c8;
  --good:#2ecc71; --bad:#e74c3c; --warn:#f1c40f; --accent:#4aa3ff; --sel:#22304a;
}
*{box-sizing:border-box}
html,body{height:100%}
body{margin:0;background:var(--bg);color:var(--txt);
     font-family:"Microsoft YaHei","PingFang SC",Segoe UI,Roboto,sans-serif;line-height:1.55}
#app{display:flex;height:100vh;overflow:hidden}
#side{width:340px;min-width:340px;border-right:1px solid var(--line);background:var(--panel);
      display:flex;flex-direction:column}
#side h1{font-size:15px;margin:0;padding:14px 16px;border-bottom:1px solid var(--line);color:#fff}
#side .meta{font-size:11.5px;color:var(--sub);padding:8px 16px;border-bottom:1px solid var(--line)}
#tree{overflow:auto;padding:8px 6px 24px;font-size:12.5px;flex:1}
#tree ul{list-style:none;margin:0;padding-left:12px}
#tree > ul{padding-left:6px}
#tree li{margin:1px 0}
#tree .dir-label{cursor:pointer;color:var(--sub);user-select:none}
#tree .dir-label::before{content:"\\25BE  ";color:#5b7bab}
#tree li.collapsed > .dir-label::before{content:"\\25B8  "}
#tree li.collapsed > ul{display:none}
#tree a{display:block;color:var(--txt);text-decoration:none;padding:2px 6px;border-radius:4px}
#tree a:hover{background:#1f2a42}
#tree a.active{background:var(--sel);color:#fff;font-weight:600}
#main{flex:1;overflow:auto;padding:26px 34px 60px}
.hdr{font-size:12px;color:var(--sub);margin-bottom:6px}
h2.file-title{font-size:20px;margin:0 0 18px;color:#fff}
.ent{background:var(--panel);border:1px solid var(--line);border-radius:8px;
     padding:16px 18px;margin-bottom:16px}
.ent h3{font-size:15px;margin:0 0 10px;color:#fff}
.ent .kind{font-size:10.5px;text-transform:uppercase;letter-spacing:.06em;color:#0f1420;
           background:var(--accent);border-radius:3px;padding:1px 6px;margin-right:8px;
           vertical-align:middle}
.e-struct .kind{background:#7de8d8} .e-enum .kind{background:var(--warn)}
.e-fn .kind{background:var(--good)} .e-macro .kind{background:#c58fff}
.e-alias .kind{background:#9fb0c8} .e-ns .kind{background:#2a3550;color:var(--sub)}
.doc{font-size:12.5px;color:#cfe0ff;margin:0 0 10px}
.doc p{margin:2px 0}
.doc.small{font-size:11.5px;color:var(--sub);margin:2px 0 0 46px}
h4{font-size:12px;margin:14px 0 6px;color:var(--sub);text-transform:uppercase;letter-spacing:.06em}
.sig{font-size:12px;padding:3px 0;border-top:1px solid #202b42}
.sig code{background:transparent;color:#dbe7f7;font-family:Consolas,monospace;font-size:12px}
.acc{display:inline-block;min-width:40px;font-size:10px;border-radius:3px;padding:0 5px;
     margin-right:6px;text-align:center}
.a-public{background:#1d3326;color:#7de8a0} .a-protected{background:#33291a;color:#ffd479}
.a-private{background:#36201f;color:#ff9d92}
pre.src{background:#121a2a;border:1px solid var(--line);border-radius:6px;padding:12px;
        overflow:auto;font-family:Consolas,monospace;font-size:12px;color:#cfe0ff;margin:0}
.kw{color:#ffd479} .cm{color:#6f8099;font-style:italic} .st{color:#9fe0a0}
code{background:#121a2a;padding:1px 5px;border-radius:3px;color:#9fe0a0;
     font-family:Consolas,monospace;font-size:12px}
.empty{color:var(--sub);font-size:13px}
"""

JS = """
(function(){
  var links = document.querySelectorAll('#tree a[data-file]');
  var panes = document.querySelectorAll('.pane');
  function show(anchor){
    for (var i=0;i<panes.length;i++){ panes[i].style.display = (panes[i].id === 'pane-' + anchor) ? '' : 'none'; }
    for (var j=0;j<links.length;j++){ links[j].classList.toggle('active', links[j].dataset.file === anchor); }
    document.getElementById('main').scrollTop = 0;
  }
  for (var i=0;i<links.length;i++){
    links[i].addEventListener('click', function(ev){
      ev.preventDefault();
      show(this.dataset.file);
      if (history.replaceState) { history.replaceState(null, '', '#' + this.dataset.file); }
    });
  }
  var labels = document.querySelectorAll('#tree .dir-label');
  for (var k=0;k<labels.length;k++){
    labels[k].addEventListener('click', function(){
      this.parentElement.classList.toggle('collapsed');
    });
  }
  var first = links.length ? links[0].dataset.file : null;
  if (location.hash && location.hash.length > 1) { show(location.hash.slice(1)); }
  else if (first) { show(first); }
})();
"""

PAGE = """<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Maho · Source</title>
<style>{css}</style>
</head>
<body>
<div id="app">
  <div id="side">
    <h1>Maho · Source</h1>
    <div class="meta">{meta}</div>
    <div id="tree">{tree}</div>
  </div>
  <div id="main">{panes}</div>
</div>
<script>{js}</script>
</body>
</html>
"""


def build(Root: Path, Out: Path) -> tuple[int, int, int]:
	Headers: list[FHeader] = []
	for Path_ in sorted(Root.rglob("*.h")):
		if any(Part in {".vs", "Intermediate", "Binaries"} for Part in Path_.parts):
			continue
		Rel = Path_.relative_to(Root).as_posix()
		Headers.append(parse_header(Path_, Rel))

	Panes: list[str] = []
	EntityCount = 0
	MemberCount = 0
	for H in Headers:
		Anchor = H.Rel.replace("/", "__").replace(".", "_")
		Panes.append(f'<div class="pane" id="pane-{Anchor}">')
		Panes.append(f'<div class="hdr">{html.escape(H.Rel)} · {H.Lines} 行</div>')
		Panes.append(f'<h2 class="file-title">{html.escape(Path(H.Rel).name)}</h2>')
		if not H.Entities:
			Panes.append('<div class="empty">（未解析出顶层实体）</div>')
		for Index, Entity in enumerate(H.Entities):
			EntityCount += 1
			MemberCount += len(Entity.Members)
			Panes.append(render_entity(Entity, f"{Anchor}-{Index}"))
		Panes.append("</div>")

	Meta = (f'{len(Headers)} 个头文件 · {EntityCount} 个顶层实体 · {MemberCount} 个成员'
	        f'<br>生成：Tools/gen_source_docs.py（扫描 Source/**/*.h）')
	Text = PAGE.format(css=CSS, js=JS, meta=Meta, tree=render_tree(Headers), panes="\n".join(Panes))
	Out.parent.mkdir(parents=True, exist_ok=True)
	Out.write_text(Text, encoding="utf-8", newline="\n")
	return len(Headers), EntityCount, MemberCount


def main() -> int:
	Parser = argparse.ArgumentParser()
	Parser.add_argument("--root", default="Source")
	Parser.add_argument("--out", default=None)
	Args = Parser.parse_args()
	Root = Path(Args.root).resolve()
	Out = Path(Args.out).resolve() if Args.out else (Root / "Docs.html")
	Headers, Entities, Members = build(Root, Out)
	print(f"[gen_source_docs] {Root} -> {Out}")
	print(f"[gen_source_docs] {Headers} headers, {Entities} entities, {Members} members")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
