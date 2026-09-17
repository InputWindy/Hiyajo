#!/usr/bin/env python3
# Run via Tools/maho_python.bat — engine Tools/python only.
"""
docs_builder.py —— 文档构建器（声明式原子接口）

不扫描源码，不解析 C++。文档内容由人**一条一条声明**（见 Tools/docs_content.py），
本模块只提供原子函数 + 渲染成单文件自包含的 Source/Docs.html。

原子接口（都在当前上下文里追加）：

    Header("Public/Core/FrameGraph.h")   一个头文件 —— 左树里的叶子（路径决定树的嵌套）
    Class("FFrameGraph", Base="...")     一个类      （当前类随之切换）
    Struct / Enum / Macro / Alias        同类，不同 kind
    Interface("bool Submit(...);")       当前类的一个接口（成员函数）
    Field("TaskKey Key")                 当前类的一个字段（数据成员）
    Nested("FEdge", Kind="struct")       当前类里的嵌套类型
    SetAccess("private")                 之后 Interface/Field/Nested 的访问级别（默认 public）

    Build()                              渲染到 Source/Docs.html，返回统计

用法：
  Tools\\maho_python.bat Tools\\docs_content.py            # 渲染 Source/Docs.html
  Tools\\maho_python.bat Tools\\docs_content.py --out X.html
"""

from __future__ import annotations

import html
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

# ── 模型 ────────────────────────────────────────────────────────────────────────


@dataclass
class FMember:
	Kind: str = "field"            # field | function | class | struct | enum
	Signature: str = ""
	Desc: list[str] = field(default_factory=list)
	Access: str = "public"


@dataclass
class FEntity:
	Kind: str = "class"            # class | struct | enum | macro | alias
	Name: str = ""
	Desc: list[str] = field(default_factory=list)
	Base: str = ""                 # 可选的继承/目标/宏体，按 kind 解释
	Members: list[FMember] = field(default_factory=list)


@dataclass
class FCard:
	"""一个卡片：标题 + 说明 + 一张表（列标题 + 若干行）。"""
	Title: str = ""
	Desc: list[str] = field(default_factory=list)
	Columns: list[str] = field(default_factory=list)
	Rows: list[list[str]] = field(default_factory=list)


@dataclass
class FHeader:
	Rel: str = ""                  # 相对 Source/ 的路径，例如 Public/Core/FrameGraph.h
	Title: str = ""
	Desc: list[str] = field(default_factory=list)
	Cards: list[FCard] = field(default_factory=list)      # 头级卡片（表格等），排在实体之前
	Entities: list[FEntity] = field(default_factory=list)


_HEADERS: list[FHeader] = []
_CURRENT_HEADER: FHeader | None = None
_CURRENT_ENTITY: FEntity | None = None
_CURRENT_CARD: FCard | None = None
_CURRENT_ACCESS = "public"


def Reset() -> None:
	"""清空已声明的全部内容（重新构建一份文档时用）。"""
	global _HEADERS, _CURRENT_HEADER, _CURRENT_ENTITY, _CURRENT_CARD, _CURRENT_ACCESS
	_HEADERS = []
	_CURRENT_HEADER = None
	_CURRENT_ENTITY = None
	_CURRENT_CARD = None
	_CURRENT_ACCESS = "public"


def _lines(Text: Any) -> list[str]:
	if Text is None:
		return []
	if isinstance(Text, (list, tuple)):
		Out: list[str] = []
		for Item in Text:
			Out.extend(_lines(Item))
		return Out
	return [Line.rstrip() for Line in str(Text).strip("\n").splitlines()]


# ── 原子接口 ────────────────────────────────────────────────────────────────────


def Header(Rel: str, Title: str = "", Desc: Any = "") -> FHeader:
	"""声明一个头文件：左树的叶子。路径里的文件夹自动成为树的层级。"""
	global _CURRENT_HEADER, _CURRENT_ENTITY
	Node = FHeader(Rel=Rel.strip("/"), Title=Title or Path(Rel).name, Desc=_lines(Desc))
	_HEADERS.append(Node)
	_CURRENT_HEADER = Node
	_CURRENT_ENTITY = None
	return Node


def _entity(Kind: str, Name: str, Base: str = "", Desc: Any = "") -> FEntity:
	global _CURRENT_ENTITY, _CURRENT_ACCESS
	if _CURRENT_HEADER is None:
		raise RuntimeError("先调用 Header(...) 才能加类/结构/枚举")
	Node = FEntity(Kind=Kind, Name=Name, Desc=_lines(Desc), Base=Base)
	_CURRENT_HEADER.Entities.append(Node)
	_CURRENT_ENTITY = Node
	_CURRENT_ACCESS = "public"
	return Node


def Class(Name: str, Base: str = "", Desc: Any = "") -> FEntity:
	"""一个类。之后的 Interface / Field / Nested 都挂到它下面。"""
	return _entity("class", Name, Base, Desc)


def Struct(Name: str, Base: str = "", Desc: Any = "") -> FEntity:
	return _entity("struct", Name, Base, Desc)


def Enum(Name: str, Base: str = "", Desc: Any = "") -> FEntity:
	return _entity("enum", Name, Base, Desc)


def Macro(Name: str, Body: str = "", Desc: Any = "") -> FEntity:
	return _entity("macro", Name, Body, Desc)


def Alias(Name: str, Target: str = "", Desc: Any = "") -> FEntity:
	return _entity("alias", Name, Target, Desc)


def Card(Title: str, Desc: Any = "") -> FCard:
	"""在当前头里加一个卡片（排在实体之前）。之后的 Table / Row 都挂到它下面。"""
	global _CURRENT_CARD
	if _CURRENT_HEADER is None:
		raise RuntimeError("先调用 Header(...) 才能加卡片")
	Node = FCard(Title=Title, Desc=_lines(Desc))
	_CURRENT_HEADER.Cards.append(Node)
	_CURRENT_CARD = Node
	return Node


def Table(*Columns: str) -> FCard:
	"""给当前卡片声明表头（列名）。"""
	if _CURRENT_CARD is None:
		raise RuntimeError("先调用 Card(...) 才能加表格")
	_CURRENT_CARD.Columns = [str(C) for C in Columns]
	return _CURRENT_CARD


def Row(*Cells: Any) -> FCard:
	"""给当前卡片加一行（单元格按顺序对应表头）。"""
	if _CURRENT_CARD is None:
		raise RuntimeError("先调用 Card(...) 才能加行")
	_CURRENT_CARD.Rows.append([str(C) for C in Cells])
	return _CURRENT_CARD


def RenderCard(CardNode: FCard) -> str:
	Out = ['<div class="card">']
	if CardNode.Title:
		Out.append(f'<h3 class="card-title">{html.escape(CardNode.Title)}</h3>')
	Out.append(RenderDesc(CardNode.Desc))
	if CardNode.Columns:
		Out.append("<table>")
		Out.append("<thead><tr>"
		           + "".join(f"<th>{Highlight(C)}</th>" for C in CardNode.Columns)
		           + "</tr></thead>")
		Out.append("<tbody>")
		for Row_ in CardNode.Rows:
			Out.append("<tr>" + "".join(f"<td>{Highlight(C)}</td>" for C in Row_) + "</tr>")
		Out.append("</tbody></table>")
	Out.append("</div>")
	return "\n".join(Out)


def SetAccess(Level: str) -> None:
	"""切换当前类里后续成员的访问级别（public / protected / private）。"""
	global _CURRENT_ACCESS
	Level = Level.strip().lower()
	if Level not in ("public", "protected", "private"):
		raise ValueError("访问级别只能是 public / protected / private")
	_CURRENT_ACCESS = Level


def _member(Kind: str, Signature: str, Desc: Any) -> FMember:
	if _CURRENT_ENTITY is None:
		raise RuntimeError("先调用 Class/Struct/Enum(...) 才能加成员")
	Node = FMember(Kind=Kind, Signature=" ".join(Signature.split()), Desc=_lines(Desc),
	               Access=_CURRENT_ACCESS)
	_CURRENT_ENTITY.Members.append(Node)
	return Node


def Interface(Signature: str, Desc: Any = "") -> FMember:
	"""当前类的一个接口（成员函数）。Signature 原样展示，例如 "bool Submit(std::vector<FTask>);"。"""
	return _member("function", Signature, Desc)


def Field(Signature: str, Desc: Any = "") -> FMember:
	"""当前类的一个字段（数据成员），例如 "FTaskKey Key;"。"""
	return _member("field", Signature, Desc)


def Nested(Name: str, Kind: str = "struct", Desc: Any = "") -> FMember:
	"""当前类里的嵌套类型（渲染在「嵌套」分区）。"""
	return _member(Kind, Name, Desc)


# ── 渲染 ────────────────────────────────────────────────────────────────────────

KEYWORDS = (
	"alignas auto bool break case catch char class const constexpr continue default delete do "
	"double else enum explicit export extern false final float for friend if inline int long "
	"mutable namespace new noexcept nullptr operator override private protected public return "
	"short signed sizeof static struct switch template this throw true try typename union unsigned "
	"using virtual void volatile while"
).split()
KEYWORD_RE = re.compile(r"\b(" + "|".join(KEYWORDS) + r")\b")
COMMENT_RE = re.compile(r"(//[^\n]*|/\*.*?\*/)", re.DOTALL)
STRING_RE = re.compile(r"(\"(?:[^\"\\]|\\.)*\"|'(?:[^'\\]|\\.)*')")

KIND_LABELS = {"class": "类", "struct": "结构体", "enum": "枚举", "macro": "宏",
               "alias": "别名", "function": "接口", "field": "字段"}


def Highlight(Text: str) -> str:
	"""转义 + 轻着色。注释与字符串先占位，关键字只跑剩余文本，最后回填 —— 否则会在注释里
	写出嵌套 span（`class=` 会变成畸形属性）。"""
	Out = html.escape(Text, quote=False)
	Saved: list[str] = []

	def Stash(Match: re.Match) -> str:
		Cls = "cm" if Match.group(0).lstrip().startswith(("//", "/*")) else "st"
		Saved.append(f'<span class="{Cls}">' + Match.group(0) + "</span>")
		return f"\x00{len(Saved) - 1}\x00"

	Out = COMMENT_RE.sub(Stash, Out)
	Out = STRING_RE.sub(Stash, Out)
	Out = KEYWORD_RE.sub(lambda M: '<span class="kw">' + M.group(0) + "</span>", Out)
	return re.sub(r"\x00(\d+)\x00", lambda M: Saved[int(M.group(1))], Out)


def RenderDesc(Desc: list[str], Css: str = "doc") -> str:
	if not Desc:
		return ""
	Out = [f'<div class="{Css}">']
	for Line in Desc:
		Out.append(f"<p>{Highlight(Line) if Line else '&nbsp;'}</p>")
	Out.append("</div>")
	return "\n".join(Out)


def RenderMember(Member: FMember) -> str:
	Out = [f'<div class="sig"><span class="acc a-{Member.Access}">{Member.Access}</span> '
	       f"<code>{Highlight(Member.Signature)}</code></div>"]
	Out.append(RenderDesc(Member.Desc, "doc small"))
	return "\n".join(Out)


def RenderEntity(Entity: FEntity, Anchor: str) -> str:
	Label = KIND_LABELS.get(Entity.Kind, Entity.Kind)
	Out = [f'<div class="ent e-{Entity.Kind}" id="{Anchor}">',
	       f'<h3><span class="kind">{Label}</span> {html.escape(Entity.Name)}'
	       + (f' <span class="base">: {Highlight(Entity.Base)}</span>' if Entity.Base else "")
	       + "</h3>"]
	if Entity.Desc:
		Out.append('<div class="sect">' + RenderDesc(Entity.Desc) + "</div>")
	Groups = {"fields": [], "interface": [], "nested": []}
	for Member in Entity.Members:
		if Member.Kind == "function":
			Groups["interface"].append(Member)
		elif Member.Kind == "field":
			Groups["fields"].append(Member)
		else:
			Groups["nested"].append(Member)
	for Key, Title, Columns in (("fields", "字段", ("访问域", "字段", "说明")),
	                            ("interface", "接口", ("访问域", "接口签名", "说明")),
	                            ("nested", "嵌套", ("访问域", "名称", "说明"))):
		if not Groups[Key]:
			continue
		Out.append('<div class="sect">')
		Out.append(f"<h4>{Title}</h4>")
		Out.append("<table><thead><tr>"
		           + "".join(f"<th>{C}</th>" for C in Columns)
		           + "</tr></thead><tbody>")
		for Member in Groups[Key]:
			Detail = "<br>".join(Highlight(Line) for Line in Member.Desc)
			Out.append(f'<tr><td><span class="acc a-{Member.Access}">{Member.Access}</span></td>'
			           f"<td>{Highlight(Member.Signature)}</td><td>{Detail}</td></tr>")
		Out.append("</tbody></table></div>")
	Out.append("</div>")
	return "\n".join(Out)


def ScanTree(Root: str | Path = "Source") -> list[str]:
	"""左树的数据源：扫描目录，列出 Root 下全部 `.h`（相对路径，排序）。

	扫描只用于**导航** —— 它让"哪些头存在"永远和磁盘一致，不用手工维护一份文件清单。
	右侧内容不来自扫描：只有 docs_content.py 里声明过的头才有内容，其余显示"尚未声明"。
	"""
	RootPath = Path(Root)
	Out: list[str] = []
	for Path_ in sorted(RootPath.rglob("*.h")):
		if any(Part in {".vs", "Intermediate", "Binaries", "Packaged", "x64"}
		       for Part in Path_.parts):
			continue
		Out.append(Path_.relative_to(RootPath).as_posix())
	return Out


def RenderTree(Paths: list[str]) -> str:
	"""按文件夹嵌套渲染路径列表（叶子 = .h）。"""
	Tree: dict = {}
	for Rel in Paths:
		Node = Tree
		for Part in Rel.split("/")[:-1]:
			Node = Node.setdefault(Part, {})
		Node.setdefault("__files__", []).append(Rel)

	def Emit(Node: dict, Prefix: str) -> str:
		Out = ["<ul>"]
		for Key in sorted(K for K in Node if K != "__files__"):
			Sub = f"{Prefix}/{Key}" if Prefix else Key
			Out.append(f'<li class="dir"><span class="dir-label" data-dir="{html.escape(Sub)}">'
			           f"{html.escape(Key)}</span>")
			Out.append(Emit(Node[Key], Sub))
			Out.append("</li>")
		for Rel in Node.get("__files__", []):
			Anchor = AnchorOf(Rel)
			Out.append(f'<li class="file"><a href="#{Anchor}" data-file="{Anchor}">'
			           f"{html.escape(Path(Rel).name)}</a></li>")
		Out.append("</ul>")
		return "\n".join(Out)

	return Emit(Tree, "")


def AnchorOf(Rel: str) -> str:
	return Rel.replace("/", "__").replace(".", "_")


CSS = """
:root{
  --bg:#0f1420; --panel:#1a2234; --line:#2a3550; --txt:#e6ecf5; --sub:#9fb0c8;
  --good:#2ecc71; --warn:#f1c40f; --accent:#4aa3ff; --sel:#22304a;
}
*{box-sizing:border-box}
html,body{height:100%}
body{margin:0;background:var(--bg);color:var(--txt);
     font-family:"Microsoft YaHei","PingFang SC",Segoe UI,Roboto,sans-serif;line-height:1.55}
#app{display:flex;height:100vh;overflow:hidden}
#side{width:340px;min-width:340px;border-right:1px solid var(--line);background:var(--panel);
      display:flex;flex-direction:column}
#side h1{font-size:15px;margin:0;padding:14px 16px;border-bottom:1px solid var(--line);color:#fff}
#side .search{padding:10px 12px;border-bottom:1px solid var(--line)}
#side .search input{width:100%;background:#121a2a;border:1px solid var(--line);border-radius:6px;
     color:var(--txt);padding:6px 9px;font-size:12.5px;font-family:inherit}
#side .search input::placeholder{color:#5b7bab}
#side .search input:focus{outline:none;border-color:var(--accent)}
#tree li.hidden{display:none}
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
.ent .base{font-size:12px;color:var(--sub);font-weight:400}
.ent .kind{font-size:10.5px;color:#0f1420;background:var(--accent);border-radius:3px;
           padding:1px 6px;margin-right:8px;vertical-align:middle}
.e-struct .kind{background:#7de8d8} .e-enum .kind{background:var(--warn)}
.e-macro .kind{background:#c58fff} .e-alias .kind{background:#9fb0c8}
.card{background:#182238;border:1px solid var(--line);border-radius:8px;padding:14px 16px;
      margin:0 0 16px}
.card-title{font-size:14px;margin:0 0 8px;color:#fff}
.card table,.ent table{border-collapse:collapse;width:100%;font-size:12.5px}
.card th,.ent th{background:#202b42;color:var(--sub);font-weight:600;text-align:left}
.card th,.card td,.ent th,.ent td{border:1px solid var(--line);padding:6px 9px;vertical-align:top}
.card td:first-child{white-space:nowrap;font-family:Consolas,monospace;color:#9fe0a0}
.ent table td:nth-child(2){font-family:Consolas,monospace;color:#dbe7f7}
.ent table td:nth-child(1){width:74px}
/* 类 card 内的分区：描述 / 字段 / 接口 / 嵌套 各成一个有边框的块，边界一眼可见 */
.sect{border:1px solid var(--line);border-radius:6px;padding:10px 12px;margin:10px 0;
      background:#141c2c}
.sect > h4{margin:0 0 8px;color:#cfe0ff;font-size:12px;letter-spacing:.06em}
.sect .doc{margin:0}
.doc{font-size:12.5px;color:#cfe0ff;margin:0 0 10px}
.doc p{margin:2px 0}
.doc.small{font-size:11.5px;color:var(--sub);margin:2px 0 0 46px}
h4{font-size:12px;margin:14px 0 6px;color:var(--sub);letter-spacing:.06em}
.sig{font-size:12px;padding:3px 0;border-top:1px solid #202b42}
.sig code{background:transparent;color:#dbe7f7;font-family:Consolas,monospace;font-size:12px}
.acc{display:inline-block;min-width:40px;font-size:10px;border-radius:3px;padding:0 5px;
     margin-right:6px;text-align:center}
.a-public{background:#1d3326;color:#7de8a0} .a-protected{background:#33291a;color:#ffd479}
.a-private{background:#36201f;color:#ff9d92}
.kw{color:#ffd479} .cm{color:#6f8099;font-style:italic} .st{color:#9fe0a0}
code{background:#121a2a;padding:1px 5px;border-radius:3px;color:#9fe0a0;
     font-family:Consolas,monospace;font-size:12px}
.empty{color:var(--sub);font-size:13px;padding:8px 0}
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
      ev.preventDefault(); show(this.dataset.file);
      if (history.replaceState) { history.replaceState(null, '', '#' + this.dataset.file); }
    });
  }
  var labels = document.querySelectorAll('#tree .dir-label');
  for (var k=0;k<labels.length;k++){
    labels[k].addEventListener('click', function(){ this.parentElement.classList.toggle('collapsed'); });
  }
  var Query = document.getElementById('q');
  if (Query) {
    Query.addEventListener('input', function(){
      var Needle = this.value.trim().toLowerCase();
      var Files = document.querySelectorAll('#tree li.file');
      for (var f=0; f<Files.length; f++){
        var Hit = !Needle || Files[f].textContent.toLowerCase().indexOf(Needle) >= 0;
        Files[f].classList.toggle('hidden', !Hit);
      }
      var Dirs = document.querySelectorAll('#tree li.dir');
      for (var d=0; d<Dirs.length; d++){
        var Any = Dirs[d].querySelector('li.file:not(.hidden)');
        Dirs[d].classList.toggle('hidden', !Any);
        if (Needle && Any) { Dirs[d].classList.remove('collapsed'); }
      }
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
<title>Maho 引擎源码文档</title>
<style>{css}</style>
</head>
<body>
<div id="app">
  <div id="side">
    <h1>Maho 引擎源码</h1>
    <div class="search" title="{meta}">
      <input id="q" type="search" placeholder="搜索头文件…  （如 Frame、Core、Entry）" autocomplete="off">
    </div>
    <div id="tree">{tree}</div>
  </div>
  <div id="main">{panes}</div>
</div>
<script>{js}</script>
</body>
</html>
"""


def Build(Out: str | Path = "Source/Docs.html",
          Root: str | Path = "Source") -> tuple[int, int, int]:
	"""渲染。左树 = 扫描 Root 下的 .h；右侧 = 只渲染声明过的内容。

	返回 (树里的头数, 已声明的头数, 成员数)。
	"""
	Declared = {H.Rel: H for H in _HEADERS}
	Paths = ScanTree(Root)
	for Rel in Declared:
		if Rel not in Paths:
			# 声明的路径必须和扫描出来的相对路径**逐字一致**（例如 Public/Maho.h，而不是 Maho.h），
			# 否则树里会多出一个根级条目、而真正那个叶子仍然空着。报出来，别让它静默发生。
			print(f"[docs] 警告：声明的 '{Rel}' 不在 Source/ 扫描结果里（路径写错？）")
			Paths.append(Rel)

	Panes: list[str] = []
	Entities = 0
	Members = 0
	for Rel in Paths:
		Anchor = AnchorOf(Rel)
		Panes.append(f'<div class="pane" id="pane-{Anchor}">')
		Panes.append(f'<div class="hdr">{html.escape(Rel)}</div>')
		Declared_ = Declared.get(Rel)
		if Declared_ is None:
			Panes.append(f'<h2 class="file-title">{html.escape(Path(Rel).name)}</h2>')
			Hint = html.escape('（尚未声明内容 —— 在 Tools/docs_content.py 里加一条 Header("'
			                   + Rel + '")，再逐类声明 Class / Interface / Field）')
			Panes.append('<div class="empty">' + Hint + "</div>")
		else:
			Panes.append(f'<h2 class="file-title">{html.escape(Declared_.Title)}</h2>')
			if Declared_.Desc:
				Panes.append('<div class="card">' + RenderDesc(Declared_.Desc) + "</div>")
			for CardNode in Declared_.Cards:
				Panes.append(RenderCard(CardNode))
			if not Declared_.Entities and not Declared_.Cards:
				Panes.append('<div class="empty">（这个头还没有声明内容）</div>')
			for Index, Entity in enumerate(Declared_.Entities):
				Entities += 1
				Members += len(Entity.Members)
				Panes.append(RenderEntity(Entity, f"{Anchor}-{Index}"))
		Panes.append("</div>")

	Meta = (f"左侧扫描 Source/**/*.h：{len(Paths)} 个头文件 · 已声明 {len(Declared)} 个 · "
	        f"{Entities} 个实体 / {Members} 个成员 ｜ 内容在 Tools/docs_content.py 里逐条声明")
	Text = PAGE.format(css=CSS, js=JS, meta=Meta, tree=RenderTree(Paths),
	                   panes="\n".join(Panes))
	OutPath = Path(Out)
	OutPath.parent.mkdir(parents=True, exist_ok=True)
	OutPath.write_text(Text, encoding="utf-8", newline="\n")
	return len(Paths), len(Declared), Members
