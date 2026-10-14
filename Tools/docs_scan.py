#!/usr/bin/env python3
# Run via Tools/maho_python.bat — engine Tools/python only.
"""
docs_scan.py —— 从 C++ 注释生成 Source/Docs.html（**注释是唯一来源**）

实体（类/结构体/枚举/宏/别名/自由函数）、成员（接口/字段/嵌套）、访问域、签名与说明，
全部来自源码里的 `///` 注释。所以：**改注释 = 改文档**。

注释形式（Doxygen 风格子集）：

    /// @brief 一句话说明（可续行）
    /// @param  Name 参数说明
    /// @return 返回值说明
    /// @tparam T 模板参数说明

`///` 块必须是**紧贴**声明上方、期间没有空行的连续块；`//` 普通注释不参与。

**为什么是"先快照、再分析"**：tree-sitter 的 Python 绑定在"边遍历子节点边做字段查询"的用法下，
在本仓的头文件上实测会给出**无效节点**（读它的属性即 0xC0000005，且位置随访问顺序游走）；
而"单遍下钻 + 只读 type/位置"的整树遍历是稳定的。于是这里把它分成两步：
`Snapshot()` 一次性把树变成纯 Python 结构（此后不再调用任何 Node API），分析全部在快照上做。

用法：
  Tools\\maho_python.bat Tools\\docs_scan.py                       # -> Source/Docs.html
  Tools\\maho_python.bat Tools\\docs_scan.py --filter Core/Assembly.h
  Tools\\maho_python.bat Tools\\docs_scan.py --check               # 只审注释，不写 HTML
"""

from __future__ import annotations

import argparse
import bisect
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import docs_builder as D

from tree_sitter import Language, Parser
import tree_sitter_cpp

# 解析前抹掉的"噪声宏"：它们让 C++ 语法非法（`class MAHO_API FX` / `MAHO_LOG_API void F()`），而文档
# 不需要看见它们。**等长替换成空格**，行列号与源码一致（--check 报的行号可直接跳转）。
NOISE_MACROS = (
	rb"\bMAHO_[A-Z0-9_]*_API\b",
	rb"\bMAHO_API\b",
	rb"\bMAHO_EXPORT\b",
	rb"\bMAHO_IMPORT\b",
	rb"\bMAHO_NODISCARD\b",
)

DOC_PREFIX = "///"
TAG_RE = re.compile(r"@(brief|file|param|return|returns|tparam|note|warning|see)\s*(.*)", re.I)


# ── 解析 → 快照 ─────────────────────────────────────────────────────────────────


class FParser_:
	"""解析器 + 噪声宏剥离。Parser/Language 保留在实例上，寿命长过每一个 Tree。"""

	def __init__(self) -> None:
		Lang = tree_sitter_cpp.language()
		self.Language = Lang if isinstance(Lang, Language) else Language(Lang)
		self.Parser = Parser(self.Language)

	def Parse(self, Source: bytes):
		Stripped = Source
		for Pattern in NOISE_MACROS:
			Stripped = re.sub(Pattern, lambda M: b" " * (M.end() - M.start()), Stripped)
		return Stripped, self.Parser.parse(Stripped)


class FSnap:
	"""一个节点的纯 Python 快照：类型、行号、字节区间、子节点。`Text` 由源码切片现算。"""

	__slots__ = ("Type", "Row", "Start", "End", "Kids")

	def __init__(self, Type: str, Row: int, Start: int, End: int) -> None:
		self.Type = Type
		self.Row = Row
		self.Start = Start
		self.End = End
		self.Kids: list[FSnap] = []

	def Find(self, *Types: str):
		for Kid in self.Kids:
			if Kid.Type in Types:
				return Kid
		return None

	def All(self, *Types: str) -> list:
		return [Kid for Kid in self.Kids if Kid.Type in Types]

	def Text(self, Source: bytes) -> str:
		return " ".join(Source[self.Start:self.End].decode("utf-8", "replace").split())


def Snapshot(Tree, Source: bytes) -> FSnap:
	"""整树 → 快照。**一遍显式栈下钻，每次只读 type / 字节区间**（绑定层在这个用法下稳定）。

	行号**由字节偏移自己算**（源码里每个 `\\n` 的位置 + 二分）：绑定层给出的 `start_point` 在同一次
	遍历里也会偶发错值（实测同一份 `FrameGraph.h` 报出过 8967 这种行号），而字节区间是可用的，
	行号又是纯算术 —— 不依赖它反而更准。"""
	LineStarts = [0]
	for Index, Byte in enumerate(Source):
		if Byte == 0x0A:
			LineStarts.append(Index + 1)

	Root = Tree.root_node

	def Make(Node) -> FSnap:
		Start = Node.start_byte
		return FSnap(Node.type, bisect.bisect_right(LineStarts, Start) - 1, Start, Node.end_byte)

	Top = Make(Root)
	Stack = [(Root, Top)]
	while Stack:
		Node, Snap = Stack.pop()
		for Index in range(Node.child_count):
			Child = Node.child(Index)
			ChildSnap = Make(Child)
			Snap.Kids.append(ChildSnap)
			Stack.append((Child, ChildSnap))
	return Top


def IsError(Node: FSnap) -> bool:
	return Node.Type == "ERROR" or Node.Type == "MISSING"


def FirstError(Node: FSnap) -> int:
	Stack = [Node]
	while Stack:
		Current = Stack.pop()
		if IsError(Current):
			return Current.Row
		Stack.extend(Current.Kids)
	return -1


# ── 注释 ────────────────────────────────────────────────────────────────────────


def DocLines(Node: FSnap, Rows: list[str]) -> list[str]:
	"""紧贴 Node 上方的 `///` 连续块（没有空行、且块内全是 `///`）。行表整头只拆一次。"""
	Index = Node.Row - 1
	Lines: list[str] = []
	while 0 <= Index < len(Rows):
		Text = Rows[Index].strip()
		if not Text.startswith(DOC_PREFIX):
			break
		Lines.append(Text)
		Index -= 1
	Lines.reverse()
	return Lines


def ParseDoc(Lines: list[str]) -> tuple[str, list[tuple[str, str]]]:
	"""`///` 块 → (散行, [(tag, text)])。续行并入上一条 tag；tag 之前的散行是第一段说明。"""
	Brief = ""
	Tags: list[tuple[str, str]] = []
	Current: int | None = None
	for Raw in Lines:
		Body = Raw.lstrip()[len(DOC_PREFIX):].strip()
		if not Body:
			continue
		Match = TAG_RE.match(Body)
		if Match:
			Tags.append((Match.group(1).lower(), Match.group(2).strip()))
			Current = len(Tags) - 1
			continue
		if Current is not None:
			Tag, Text = Tags[Current]
			Tags[Current] = (Tag, (Text + " " + Body).strip())
		else:
			Brief = (Brief + " " + Body).strip()
	return Brief, Tags


def DescOf(Brief: str, Tags: list[tuple[str, str]]) -> list[str]:
	"""说明行的渲染顺序：brief 在前，`@param`/`@tparam`/`@return` 依源码顺序在后。"""
	Out: list[str] = []
	Head = " ".join([Brief] + [Text for Tag, Text in Tags if Tag in ("brief", "file") and Text]).strip()
	if Head:
		Out.append(Head)
	for Tag, Text in Tags:
		if not Text or Tag in ("brief", "file"):
			continue
		if Tag in ("param", "tparam"):
			Name, _, Rest = Text.partition(" ")
			Out.append(f"`{Name}`：{Rest.strip()}" if Rest.strip() else f"`{Name}`")
		elif Tag in ("return", "returns"):
			Out.append(f"返回：{Text}")
		elif Tag == "note":
			Out.append(f"注：{Text}")
		elif Tag == "warning":
			Out.append(f"警告：{Text}")
		elif Tag == "see":
			Out.append(f"参见：{Text}")
		else:
			Out.append(Text)
	return Out


def FileDoc(Rows: list[str]) -> list[str]:
	"""文件说明 = 头里**带 `@file` 标记**的那个 `///` 块（通常放最上面）。

	口径是显式的：普通 `//` 注释是代码注释，**不进文档**；`///` 块只有带 `@file` 才代表"这是这个头
	文件的说明"，其余 `///` 块一律属于紧贴其下的那个声明。写法：

	    /// @file
	    /// @brief 一句话说明；续行照写。
	"""
	Index = 0
	while Index < len(Rows):
		if not Rows[Index].strip().startswith(DOC_PREFIX):
			Index += 1
			continue
		Block: list[str] = []
		while Index < len(Rows) and Rows[Index].strip().startswith(DOC_PREFIX):
			Block.append(Rows[Index].strip())
			Index += 1
		if any(re.match(r"///\s*@file\b", Line, re.I) for Line in Block):
			return [Line for Line in Block if not re.match(r"///\s*@file\b", Line, re.I)]
	return []


# ── 声明的形状（都在快照上判类型，不做字段查询）─────────────────────────────────

NAME_TYPES = ("identifier", "type_identifier", "field_identifier", "qualified_identifier")
BODY_TYPES = ("field_declaration_list", "declaration_list", "compound_statement")
DECLARATION_TYPES = ("class_specifier", "struct_specifier", "union_specifier", "enum_specifier",
                     "namespace_definition", "function_definition", "declaration", "field_declaration",
                     "alias_declaration", "type_definition", "template_declaration",
                     "preproc_def", "preproc_function_def")
DECL_TYPES = ("declarator", "function_declarator", "init_declarator", "reference_declarator",
              "pointer_declarator", "attributed_declarator")
TEMPLATE_TYPES = ("template_declaration",)
VALUE_TYPES = ("preproc_arg", "type_descriptor", "primitive_type", "sized_type_specifier",
               "type_identifier", "qualified_identifier", "template_type", "number_literal",
               "string_literal", "true", "false")


MACRO_CALL_RE = re.compile(r"^[A-Z][A-Z0-9_]*\s*\(")


def IsMacroCallSignature(Signature: str) -> bool:
	"""`MAHO_DECLARE_FRAME(FLog);` 这类**纯宏调用行**：它是声明性的（"这个类在此声明自己"），
	但不是一个可以有 `@brief` 的成员 —— 不参与缺注释统计，仍列进文档。

	判据用**签名形态**（全大写标识符紧跟左括号），而不是语法节点：解析器会把它读成"声明了一个叫
	`MAHO_DECLARE_FRAME` 的函数"，节点层面和真函数声明无法区分；宏的拼写却能一眼认出。"""
	return bool(MACRO_CALL_RE.match(Signature.strip()))


def SignatureOf(Node: FSnap, Source: bytes) -> str:
	"""声明的展示文本：去掉函数体、压掉换行；`= delete` / `= 0` 这类尾巴保留。"""
	Body = Node.Find(*BODY_TYPES)
	Cut = Body.Start if Body is not None else Node.End
	Text = Source[Node.Start:Cut].decode("utf-8", "replace")
	return " ".join(Text.replace("{", " ").replace("}", " ").split()).rstrip(";").strip()


def TemplatePrefix(Node: FSnap, Source: bytes) -> str:
	"""`template <...> ` 前缀，取自源码本身（不自己拼尖括号）。"""
	Params = Node.Find("template_parameter_list")
	if Params is None:
		return ""
	return Source[Node.Start:Params.End].decode("utf-8", "replace").strip() + " "


# ── 扫描 ────────────────────────────────────────────────────────────────────────


class FScanner:
	def __init__(self, Root: Path, Options) -> None:
		self.Root = Root
		self.Options = Options
		self.Parser = FParser_()
		self.Stats = {"headers": 0, "entities": 0, "members": 0, "documented": 0, "missing": 0}
		self.Problems: list[str] = []
		self.bFunctionsOpen = False

	def Report(self, Rel: str, Node: FSnap, Message: str) -> None:
		self.Problems.append(f"{Rel}:{Node.Row + 1}: {Message}")

	def Headers(self) -> list[Path]:
		Out = []
		for Path_ in sorted(self.Root.rglob("*.h")):
			if any(Part in {".vs", "Intermediate", "Binaries", "Packaged", "x64"}
			       for Part in Path_.parts):
				continue
			Rel = Path_.relative_to(self.Root).as_posix()
			if self.Options.filter and self.Options.filter not in Rel:
				continue
			Out.append(Path_)
		return Out

	def ScanHeader(self, Path_: Path) -> None:
		Rel = Path_.relative_to(self.Root).as_posix()
		if self.Options.verbose:
			print(f"[docs_scan] 扫描 {Rel}", flush=True)
		Source, Tree = self.Parser.Parse(Path_.read_bytes())
		Root = Snapshot(Tree, Source)
		Rows = Source.decode("utf-8", "replace").split("\n")

		ErrorRow = FirstError(Root)
		if ErrorRow >= 0 and not self.Options.quiet:
			print(f"[docs_scan] {Rel}:{ErrorRow + 1} 解析有 ERROR —— 按已解析到的部分生成")

		FileLines = FileDoc(Rows)
		Brief, Tags = ParseDoc(FileLines)
		D.Header(Rel, Desc=DescOf(Brief, Tags))
		if not FileLines and not self.Options.quiet:
			self.Report(Rel, Root, "缺 @file：这个头没有文件级说明（`/// @file` 块）")
		self.DeclareIncludes(Rows)
		self.Stats["headers"] += 1
		self.bFunctionsOpen = False          # 「自由函数」一个头只开一次（不是一个函数一个）
		self.Walk(Root, Source, Rows, Rel)

	def DeclareIncludes(self, Rows: list[str]) -> None:
		"""「包含的头文件」卡片：**由源码派生**，不再手写。

		它回答的是"这个头依赖谁"，答案就在文件里；手写那张表的散文部分（"这个头提供什么"）属于
		`@file` 块，不重复一遍。"""
		Includes = [M.group(1) for Line in Rows
		            if (M := re.match(r"\s*#include\s*([<\"][^>\"]+[>\"])", Line))]
		if not Includes:
			return
		D.Card("包含的头文件")
		D.Table("头文件")
		for Include in Includes:
			D.Row(f"`{Include}`")

	def Walk(self, Node: FSnap, Source: bytes, Rows: list[str], Rel: str, Template: str = "") -> None:
		for Kid in Node.Kids:
			if Kid.Type == "namespace_definition":
				Body = Kid.Find("declaration_list")
				if Body is not None:
					self.Walk(Body, Source, Rows, Rel)
				continue
			if Kid.Type == "linkage_specification":
				Inner = [C for C in Kid.Kids if C.Type == "declaration_list"]
				if Inner:
					self.Walk(Inner[0], Source, Rows, Rel)
				continue
			self.Declare(Kid, Source, Rows, Rel, Template)

	def Declare(self, Node: FSnap, Source: bytes, Rows: list[str], Rel: str,
	            Template: str = "", Doc: tuple[str, list] | None = None) -> None:
		if Node.Type in TEMPLATE_TYPES:
			Inner = [C for C in Node.Kids if C.Type not in ("comment", "template_declaration")]
			if not Inner:
				return
			self.Declare(Inner[-1], Source, Rows, Rel, TemplatePrefix(Node, Source),
			             Doc if Doc is not None else ParseDoc(DocLines(Node, Rows)))
			return
		if Node.Type in ("comment", "preproc_include"):
			return

		Brief, Tags = Doc if Doc is not None else ParseDoc(DocLines(Node, Rows))
		Desc = DescOf(Brief, Tags)
		Documented = bool(Desc)

		if Node.Type in ("class_specifier", "struct_specifier", "union_specifier"):
			self.DeclareClass(Node, Source, Rows, Rel, Template, Desc)
		elif Node.Type == "enum_specifier":
			Name = Node.Find(*NAME_TYPES)
			if Name is None:
				return
			D.Enum(Name.Text(Source), Base="", Desc=Desc)
			self.Stats["entities"] += 1
		elif Node.Type in ("preproc_def", "preproc_function_def"):
			Name = Node.Find(*NAME_TYPES)
			Value = Node.Find(*VALUE_TYPES)
			D.Macro(Name.Text(Source) if Name is not None else "?",
			        Value.Text(Source) if Value is not None else "", Desc=Desc)
			self.Stats["entities"] += 1
		elif Node.Type == "alias_declaration":
			Name = Node.Find(*NAME_TYPES)
			Value = Node.Find(*VALUE_TYPES)
			D.Alias(Name.Text(Source) if Name is not None else "?",
			        Value.Text(Source) if Value is not None else "", Desc=Desc)
			self.Stats["entities"] += 1
		elif Node.Type == "type_definition":
			D.Alias(SignatureOf(Node, Source), "", Desc=Desc)
			self.Stats["entities"] += 1
		elif Node.Type in ("declaration", "function_definition", "field_declaration"):
			Signature = Template + SignatureOf(Node, Source) + ";"
			if IsMacroCallSignature(Signature):
				if not self.bFunctionsOpen:
					D.Functions("")
					self.bFunctionsOpen = True
				D.Field(Signature, Desc=Desc)
				self.Stats["members"] += 1
				return
			if not any(C.Type in DECL_TYPES for C in Node.Kids):
				return
			if not self.bFunctionsOpen:
				D.Functions("")            # 名字留空：标签已经是「自由函数」，不必再重复一遍
				self.bFunctionsOpen = True
			D.Interface(Signature, Desc=Desc)
			self.Stats["members"] += 1
			if not Documented and not self.Options.quiet:
				self.Report(Rel, Node, "自由函数缺 @brief")

	def DeclareClass(self, Node: FSnap, Source: bytes, Rows: list[str], Rel: str, Template: str,
	                 Desc: list[str]) -> None:
		Name = Node.Find(*NAME_TYPES)
		if Name is None:                                  # 匿名 struct/union
			return
		Kind = D.Class if Node.Type == "class_specifier" else D.Struct
		Base = Node.Find("base_class_clause")
		Kind((Template + Name.Text(Source)).strip(),
		     Base=Base.Text(Source) if Base is not None else "", Desc=Desc)
		self.Stats["entities"] += 1

		Body = Node.Find("field_declaration_list")
		if Body is None:
			return
		D.SetAccess("private" if Node.Type == "class_specifier" else "public")
		for Member in Body.Kids:
			if Member.Type == "access_specifier":
				D.SetAccess(Member.Text(Source).rstrip(":"))
				continue
			self.DeclareMember(Member, Source, Rows, Rel, Template)

	def DeclareMember(self, Node: FSnap, Source: bytes, Rows: list[str], Rel: str,
	                  Template: str = "", Doc: tuple[str, list] | None = None) -> None:
		if Node.Type == "comment":
			return
		if Node.Type in TEMPLATE_TYPES:
			Inner = [C for C in Node.Kids if C.Type not in ("comment", "template_declaration")]
			if not Inner:
				return
			self.DeclareMember(Inner[-1], Source, Rows, Rel,
			                   Template + TemplatePrefix(Node, Source),
			                   Doc if Doc is not None else ParseDoc(DocLines(Node, Rows)))
			return

		Brief, Tags = Doc if Doc is not None else ParseDoc(DocLines(Node, Rows))
		Desc = DescOf(Brief, Tags)
		Documented = bool(Desc)

		if Node.Type in ("class_specifier", "struct_specifier", "enum_specifier", "union_specifier"):
			Name = Node.Find(*NAME_TYPES)
			D.Nested(Name.Text(Source) if Name is not None else "（匿名）",
			         Type=Node.Type.split("_")[0], Desc=Desc)
			self.Stats["members"] += 1
			Body = Node.Find("field_declaration_list")
			if Body is not None:                          # 嵌套类自己带成员
				D.SetAccess("private" if Node.Type == "class_specifier" else "public")
				for Sub in Body.Kids:
					if Sub.Type == "access_specifier":
						D.SetAccess(Sub.Text(Source).rstrip(":"))
						continue
					self.DeclareMember(Sub, Source, Rows, Rel, Template)
			return

		if Node.Type == "alias_declaration":
			Name = Node.Find(*NAME_TYPES)
			D.Nested(Name.Text(Source) if Name is not None else "?", Type="alias", Desc=Desc)
			self.Stats["members"] += 1
			return

		if Node.Type not in ("field_declaration", "declaration", "function_definition"):
			return

		Signature = Template + SignatureOf(Node, Source) + ";"
		if IsMacroCallSignature(Signature):      # 纯宏调用行：列出，但不要求 @brief（见该函数）
			D.Field(Signature, Desc=Desc)
			self.Stats["members"] += 1
			return
		if any(C.Type in ("function_declarator", "function_definition") for C in Node.Kids) \
			or Node.Type == "function_definition":
			D.Interface(Signature, Desc=Desc)
		else:
			D.Field(Signature, Desc=Desc)
		self.Stats["members"] += 1
		if Documented:
			self.Stats["documented"] += 1
		else:
			self.Stats["missing"] += 1
			if not self.Options.quiet:
				self.Report(Rel, Node, f"缺 @brief：{Signature[:60]}")
		Declared = [T for T in Tags if T[0] == "param"]
		Params = Node.Find("parameter_list")
		if Declared and Params is not None:
			Actual = len([C for C in Params.Kids if C.Type == "parameter_declaration"])
			if Actual and Actual != len(Declared):
				self.Report(Rel, Node, f"@param 数量 {len(Declared)} ≠ 形参数量 {Actual}：{Signature[:60]}")


# ── 子进程隔离（渲染路径）───────────────────────────────────────────────────────

ENTITY_CALL = {"class": D.Class, "struct": D.Struct, "enum": D.Enum, "macro": D.Macro,
               "alias": D.Alias, "functions": D.Functions}


def Feed(Header: dict) -> None:
	"""把子进程交回的模型重放进 docs_builder（纯数据 → 渲染，主进程不碰解析器）。"""
	D.Header(Header["Rel"], Desc=Header.get("Desc", []))
	for Card in Header.get("Cards", []):
		D.Card(Card.get("Title", ""), Card.get("Desc", []))
		if Card.get("Columns"):
			D.Table(*Card["Columns"])
			for Row in Card.get("Rows", []):
				D.Row(*Row)
	for Entity in Header.get("Entities", []):
		Call = ENTITY_CALL.get(Entity["Type"])
		if Call is None:
			continue
		if Entity["Type"] == "functions":
			D.Functions(Entity["Name"], Entity.get("Desc", []))
		else:
			Call(Entity["Name"], Entity.get("Base", ""), Entity.get("Desc", []))
		for Member in Entity.get("Members", []):
			D.SetAccess(Member.get("Access", "public"))
			if Member["Type"] == "function":
				D.Interface(Member["Signature"], Member.get("Desc", []))
			elif Member["Type"] == "field":
				D.Field(Member["Signature"], Member.get("Desc", []))
			else:
				D.Nested(Member["Signature"], Member["Type"], Member.get("Desc", []))


def DeclarePluginCard(Root: Path) -> None:
	"""插件页的根卡片：`.cplugin` 的事实（名字 / 说明 / 依赖），**从文件派生**，不手写。

	泛化地遍历 JSON 的键值（跳过 FileVersion），所以 `.cplugin` 将来加字段这里不用改。"""
	Files = list(Root.glob("*.cplugin"))
	if len(Files) != 1:
		return
	import json
	try:
		Data = json.loads(Files[0].read_text(encoding="utf-8"))
	except (OSError, ValueError) as Error:
		print(f"[docs_scan] {Files[0].name} 读不了：{Error}")
		return
	D.RootCard(f"{Files[0].name} —— 插件声明")
	D.Table("字段", "值")
	for Key, Value in Data.items():
		if Key == "FileVersion":
			continue
		Text = ", ".join(str(V) for V in Value) if isinstance(Value, list) else str(Value)
		D.Row(Key, Text if Text else "（空）")


def RunIsolated(Root: Path, Options) -> int:
	"""扫描路径：**每个头一个子进程**做 tree-sitter，主进程只收 JSON（`--check` 汇总问题，否则渲染）。

	理由是实测结论：tree-sitter 0.26 的 Python 绑定在长命进程里、或"边遍历边查注释/字段"的用法下
	偶发 0xC0000005；"一个头一个短命进程 + 单遍快照"的组合稳定。代价是 N 次解释器启动，对一份
	文档生成可以接受；收益是一个头崩了只丢一个头，并给出明确的失败名单。"""
	import json
	import subprocess

	Stats = {"headers": 0, "entities": 0, "members": 0, "documented": 0, "missing": 0}
	Failures: list[str] = []
	Problems: list[str] = []
	Script = str(Path(__file__).resolve())

	Paths = [P for P in sorted(Root.rglob("*.h"))
	         if not any(Part in {".vs", "Intermediate", "Binaries", "Packaged", "x64"}
	                    for Part in P.parts)]
	if Options.limit > 0:
		Paths = Paths[:Options.limit]

	D.Reset()
	for Path_ in Paths:
		Rel = Path_.relative_to(Root).as_posix()
		if Options.filter and Options.filter not in Rel:
			continue
		Proc = subprocess.run([sys.executable, Script, "--root", str(Root), "--one", Rel, "--json"],
		                      capture_output=True, text=True, encoding="utf-8", errors="replace")
		Line = (Proc.stdout or "").strip().splitlines()
		if not Line:
			Failures.append(Rel)
			continue
		Payload = json.loads(Line[-1])
		for Header in Payload["model"]:
			Feed(Header)
		Problems.extend(Payload.get("problems", []))
		for Key in Stats:
			Stats[Key] += Payload["stats"].get(Key, 0)
		if Options.verbose:
			print(f"[docs_scan] {Rel}  {Stats['members']} 个成员", flush=True)

	if Options.check:
		for Line in Problems:
			print(f"  {Line}")
		print(f"[docs_scan] --check：{Stats['headers']} 个头 · {Stats['members']} 个成员"
		      f"（{Stats['documented']} 有 @brief，{Stats['missing']} 缺）· {len(Problems)} 条问题")
		if Failures:
			print(f"[docs_scan] 这些头扫描失败：{', '.join(Failures)}")
		return 1 if (Problems or Failures) else 0

	Out = Path(Options.out) if Options.out else Root / "Docs.html"
	DeclarePluginCard(Root)
	Headers, Declared, Members = D.Build(Out=Out, Root=Root)
	print(f"[docs_scan] {Out}  {Headers} 个头 · {Declared} 个含内容 · {Members} 个成员"
	      f"（缺 @brief {Stats['missing']} 个）")
	if Failures:
		print(f"[docs_scan] 这些头扫描失败（已跳过）：{', '.join(Failures)}")
		return 1
	return 0


# ── 入口 ────────────────────────────────────────────────────────────────────────


def main() -> int:
	Arg = argparse.ArgumentParser()
	Arg.add_argument("--root", default="Source")
	Arg.add_argument("--out", default="")
	Arg.add_argument("--filter", default="", help="只扫描路径含该子串的头")
	Arg.add_argument("--limit", type=int, default=0, help="只扫前 N 个头")
	Arg.add_argument("--check", action="store_true", help="只审注释（不写 HTML）")
	Arg.add_argument("--quiet", action="store_true", help="不逐条列缺注释的成员")
	Arg.add_argument("--verbose", action="store_true", help="逐个打印正在扫描的头")
	Arg.add_argument("--one", default="", help="只扫这一个头（相对 --root），供子进程隔离模式用")
	Arg.add_argument("--json", action="store_true", help="把模型打成 JSON 到 stdout（不渲染）")
	Options = Arg.parse_args()

	Root = Path(Options.root)
	if not Root.is_dir():
		print(f"[docs_scan] 目录不存在：{Root}")
		return 1

	if Options.json:                       # 子进程：解析一个头，交出模型 + 问题，不做任何渲染
		import dataclasses
		import json
		Options.filter = Options.one
		Options.quiet = False              # 问题要带回去给主进程汇总
		D.Reset()
		Scanner = FScanner(Root, Options)
		for Path_ in Scanner.Headers():
			Scanner.ScanHeader(Path_)
		Model = [dataclasses.asdict(Header) for Header in D.Captured()]
		print(json.dumps({"model": Model, "stats": Scanner.Stats, "problems": Scanner.Problems},
		                 ensure_ascii=False))
		return 0

	return RunIsolated(Root, Options)


if __name__ == "__main__":
	# 跳过解释器收尾：tree-sitter 绑定在 finalize 时释放 Parser/Tree 的顺序不稳，显式 flush + _exit
	# 把"扫描结果"与"收尾"解耦，退出码就等于扫描的结论。
	import os
	_Code = main()
	sys.stdout.flush()
	sys.stderr.flush()
	os._exit(_Code)
