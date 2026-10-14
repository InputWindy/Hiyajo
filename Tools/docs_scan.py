#!/usr/bin/env python3
# Run via Tools/maho_python.bat — engine Tools/python only.
"""
docs_scan.py —— 从 C++ 注释生成 Source/Docs.html（**`@` 语义标签是唯一来源**）

实体（类/结构体/枚举/宏/别名/自由函数）、成员（接口/字段/嵌套）、访问域、签名与说明，
全部来自源码里的 `///` 注释 —— 而且**只取其中带 `@` 的语义标签**：没打标的 `///` 裸行是普通
注释，照写但不进接口文档（所以"改注释 = 改文档"只对语义标签成立）。

注释形式（Doxygen 风格子集）：

    /// @file            ← 文件级：这个头文件的说明（块内再跟 @brief）
    /// @brief 一句话说明（可续行）
    /// @param  Name 参数说明
    /// @return 返回值说明
    /// @tparam T 模板参数说明
    /// @note / @warning / @see 补充说明

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
# 不带括号的宏标记：按拼写认（导出标签是 `*_API`，其余几个是引擎自己的开关标记）。
MARKER_TOKENS = (
	rb"\b[A-Z][A-Z0-9_]*_API\b",
	rb"\bMAHO_EXPORT\b",
	rb"\bMAHO_IMPORT\b",
	rb"\bMAHO_NODISCARD\b",
	# 调用约定标记：它们夹在返回类型与函数名之间（`int WINAPI WinMain(...)`），展开成 `__stdcall`；
	# 留着就是"三个标识符连排"，解析器只能报 ERROR。
	rb"\b(WINAPI|APIENTRY|CALLBACK|WINBASEAPI)\b",
)

# 函数式宏调用：全大写标识符紧跟左括号。**宏标记一律当空气** —— Maho 与 UE 的宏都无法展开，而且这些
# 拼写本来就不是合法 C++（`UENUM(BlueprintType)` 紧跟着 enum、`MAHO_DECLARE_FRAME(X);` 在类体里、
# `UPROPERTY(... meta = (EditCondition = "X"))` 带嵌套括号、`DECLARE_*_DELEGATE(...)` 的参数是
# "类型 + 块注释"），解析器会在它们那里生成 ERROR 节点，**紧跟其后的整个声明都读不到**。按形状认
# （而不是逐个项目列拼写），所以 UE / Maho / 下一个工程都适用。
MACRO_CALL_RE = re.compile(
	rb"\b(?:[A-Z][A-Z0-9_]{1,}|DECLARE_[A-Za-z0-9_]*|GENERATED_[A-Za-z0-9_]*)\s*\(")

DOC_PREFIX = "///"
TAG_RE = re.compile(r"@(brief|file|param|return|returns|tparam|note|warning|see)\s*(.*)", re.I)


# ── 解析 → 快照 ─────────────────────────────────────────────────────────────────


def DefineRanges(Source: bytes) -> list[tuple[int, int, bool]]:
	"""`#define` 指令覆盖的字节区间（含反斜杠续行），以及它是否**多行**。

	两种处理，理由是两条不同的实测：
	* **单行定义**：`#define MAHO_LOG_API MAHO_EXPORT` 里也有"标记"（`MAHO_EXPORT`），所以它同样要
	  躲开剥离器 —— 但定义本身留着，它是一条可展示的宏。
	* **多行定义**：宏体跨行（`MAHO_DECLARE_FRAME_UNDER` 的 `public:` / `{ return #FrameType; }`、
	  `MAHO_QUERY_CASTER` 的模板体）语法树吃不下，会在宏体里报 ERROR。宏标记按既定口径当空气，
	  宏体也**不是 API**（没人照它写代码，只是它的展开），所以整段抹成空格。

	`#define NAME(...)` 与一次宏调用在文本上长得一样 —— 不先区分，剥离器会把名字和参数表掏空，
	宏体就成了悬空代码（实测：`Export.h` / `Fatal.h` 两处 ERROR 全是这个原因）。"""
	Ranges: list[tuple[int, int, bool]] = []
	Pos = 0
	while Pos < len(Source):
		End = Source.find(b"\n", Pos)
		End = len(Source) if End < 0 else End
		Line = Source[Pos:End].lstrip()
		if Line.startswith(b"#") and Line[1:].lstrip().startswith(b"define"):
			Start = Pos
			bMulti = False
			while Source[Start:End].rstrip().endswith(b"\\") and End < len(Source):
				bMulti = True
				Pos = End + 1
				End = Source.find(b"\n", Pos)
				End = len(Source) if End < 0 else End
			Ranges.append((Start, End, bMulti))
		Pos = End + 1
	return Ranges


def ScanComments(Source: bytes) -> list[tuple[int, int]]:
	"""注释覆盖的字节区间（`//` 到行尾、`/* … */`）。

	**注释永不改动**，两个原因：① 抹空一个跨行的宏体时，宏体里的 `/*` 若被抹掉、而它的 `*/` 在外，
	注释就破了（实测 `/*` 51→41 / `*/` 51→42，随后的整段代码都读不到）；② 注释里的标记词
	（`MAHO_API`、`WINAPI`）被抹掉会**悄悄改掉文档文字**。"""
	Ranges: list[tuple[int, int]] = []
	Pos = 0
	while Pos < len(Source):
		Byte = Source[Pos]
		if Byte in (0x22, 0x27):                       # 字面量里的 // 与 /* 不算注释
			Quote = Byte
			Pos += 1
			while Pos < len(Source) and Source[Pos] != Quote:
				Pos += 2 if Source[Pos] == 0x5C else 1
		elif Byte == 0x2F and Pos + 1 < len(Source) and Source[Pos + 1] == 0x2F:
			Found = Source.find(b"\n", Pos)
			End = len(Source) if Found < 0 else Found
			Ranges.append((Pos, End))
			Pos = End
		elif Byte == 0x2F and Pos + 1 < len(Source) and Source[Pos + 1] == 0x2A:
			Found = Source.find(b"*/", Pos + 2)
			End = len(Source) if Found < 0 else Found + 2
			Ranges.append((Pos, End))
			Pos = End
		Pos += 1
	return Ranges


class FSpans:
	"""一组字节区间：合并 + 二分，用来回答"这一段能不能被抹"。"""

	def __init__(self, Ranges: list[tuple[int, int]]) -> None:
		Merged: list[tuple[int, int]] = []
		for Start, End in sorted(Ranges):
			if Merged and Start <= Merged[-1][1]:
				Merged[-1] = (Merged[-1][0], max(Merged[-1][1], End))
			else:
				Merged.append((Start, End))
		self.Merged = Merged
		self.Starts = [Start for Start, _ in Merged]

	def Covers(self, Start: int, End: int) -> bool:
		Index = bisect.bisect_right(self.Starts, Start) - 1
		if Index >= 0 and self.Merged[Index][1] > Start:
			return True
		Index += 1
		return Index < len(self.Merged) and self.Merged[Index][0] < End

	def Gap(self, Start: int, End: int) -> list[tuple[int, int]]:
		"""区间里**不在**保护区内的那几段（按序）。"""
		Out: list[tuple[int, int]] = []
		Pos = Start
		for RStart, REnd in self.Merged:
			if RStart >= End:
				break
			if REnd <= Pos:
				continue
			if RStart > Pos:
				Out.append((Pos, min(RStart, End)))
			Pos = max(Pos, REnd)
		if Pos < End:
			Out.append((Pos, End))
		return Out


def Blank(Buffer: bytearray, Start: int, End: int, Spans: FSpans) -> None:
	"""抹成空格：**不动换行、不动保护区**（行号与注释都保持原样）。"""
	for GapStart, GapEnd in Spans.Gap(Start, End):
		for Index in range(GapStart, GapEnd):
			if Buffer[Index] not in (0x0A, 0x0D):
				Buffer[Index] = 0x20

def StripMacroMarkers(Source: bytes) -> bytes:
	"""把宏标记当空气：全大写的函数式宏调用**整体**（含配平括号）换成等长空格。

	三条不变量（都是实测换来的）：
	* **行列号不变**：抹成空格（而不是删除），且**换行永远不动** —— 否则后面所有行号整体位移，
	  `--check` 报的行号就跳不回原文件了。
	* **注释永不改动**：注释里的标记词（`MAHO_API` / `WINAPI`）被抹掉会悄悄改掉文档文字；而抹空一个
	  跨行宏体时，宏体里的 `/*` 若被抹、它的 `*/` 在外，注释当场破掉（实测 `/*` 51→41 / `*/` 51→42）。
	* **`#define` 不是调用**：`#define NAME(...)` 与一次宏调用文本同形，单行的躲开剥离（它是一条可展示
	  的宏），多行的整段当空气（宏体不是 API，且其内部的 `public:` / `#FrameType` 语法树吃不下）。
	"""
	Defines = DefineRanges(Source)
	Comments = FSpans(ScanComments(Source))
	Protected = FSpans([(Start, End) for Start, End, _ in Defines]
	                   + [(Start, End) for Start, End in Comments.Merged])

	Out = bytearray(Source)
	# 抹空多行宏体：只避开**注释** —— 不能把"这个定义自己"也算保护区，否则它永远抹不掉。
	for Start, End, bMulti in Defines:
		if bMulti:
			Blank(Out, Start, End, Comments)

	for Pattern in MARKER_TOKENS:                   # 不带括号的标记：直接换空格
		Compiled = re.compile(Pattern)
		Match = Compiled.search(Out)
		while Match is not None:
			if not Protected.Covers(Match.start(), Match.end()):
				Blank(Out, Match.start(), Match.end(), Protected)
			Match = Compiled.search(Out, Match.end())     # 注意：第二参是 pos，不是 flags

	Index = 0
	while True:
		Match = MACRO_CALL_RE.search(Out, Index)
		if Match is None:
			return bytes(Out)
		Depth = 0
		Pos = Match.end() - 1                        # 指向那个 '('
		End: int | None = None
		while Pos < len(Out):
			Byte = Out[Pos]
			if Byte in (0x22, 0x27):                 # " 或 '：跳过整个字面量
				Quote = Byte
				Pos += 1
				while Pos < len(Out) and Out[Pos] != Quote:
					Pos += 2 if Out[Pos] == 0x5C else 1
			elif Byte == 0x2F and Pos + 1 < len(Out) and Out[Pos + 1] == 0x2A:
				Found = Out.find(b"*/", Pos + 2)      # /* ... */
				Pos = (len(Out) if Found < 0 else Found + 1)
			elif Byte == 0x2F and Pos + 1 < len(Out) and Out[Pos + 1] == 0x2F:
				Found = Out.find(b"\n", Pos)          # // ... 到行尾
				Pos = (len(Out) - 1 if Found < 0 else Found)
			elif Byte == 0x28:
				Depth += 1
			elif Byte == 0x29:
				Depth -= 1
				if Depth == 0:
					End = Pos + 1
					break
			Pos += 1
		if End is None:
			return bytes(Out)                        # 括号没配平：留给解析器去报错，不猜
		if not Protected.Covers(Match.start(), End):
			Blank(Out, Match.start(), End, Protected)
		Index = End


class FParser_:
	"""解析器 + 宏标记剥离。Parser/Language 保留在实例上，寿命长过每一个 Tree。"""

	def __init__(self) -> None:
		Lang = tree_sitter_cpp.language()
		self.Language = Lang if isinstance(Lang, Language) else Language(Lang)
		self.Parser = Parser(self.Language)

	def Parse(self, Source: bytes):
		Stripped = StripMacroMarkers(Source)
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
	return Node.Type == "ERROR"


def FirstError(Node: FSnap) -> int:
	"""第一个真正的 `ERROR` 节点所在行（不是 `MISSING`）。

	区分开是必要的：`MISSING` 是解析器自己补出来的占位（常见：缺一个分号就补一个），树照常可用；
	而 `ERROR` 才是"这里有一段读不成声明"，后者之后的部分才是真的丢。"""
	Stack = [Node]
	while Stack:
		Current = Stack.pop()
		if IsError(Current):
			return Current.Row
		Stack.extend(Current.Kids)
	return -1


# ── 注释 ────────────────────────────────────────────────────────────────────────


def DocLines(Node: FSnap, Rows: list[str]) -> list[str]:
	"""紧贴 Node 上方的文档块，返回**归一成 `///` 形式**的行（没有则空）。行表整头只拆一次。

	两种写法都收（Doxygen 的两种）：
	* `///` 连续块（期间没有空行、块内全是 `///`）；
	* 紧贴的 `/** … */` 块 —— 归一成 `///` 行后走同一条解析路径。引擎里已经写好的一批 `/** … */`
	  注释因此立刻进文档，不必为了工具把它们改成 `///`（普通 `/* … */` 不是文档，不收）。
	"""
	Index = Node.Row - 1
	Lines: list[str] = []
	while 0 <= Index < len(Rows):
		Text = Rows[Index].strip()
		if not Text.startswith(DOC_PREFIX):
			break
		Lines.append(Text)
		Index -= 1
	if Lines:
		Lines.reverse()
		return Lines

	Index = Node.Row - 1
	if Index < 0 or not Rows[Index].strip().endswith("*/"):
		return []
	Start = Index
	while Start >= 0 and "/**" not in Rows[Start]:
		if Rows[Start].strip().startswith("/*"):
			return []                            # 普通块注释：不是文档
		Start -= 1
	if Start < 0:
		return []
	Out: list[str] = []
	for Line in Rows[Start:Index + 1]:
		Text = Line.strip()
		for Prefix in ("/**", "/*", "*/"):
			if Text.startswith(Prefix):
				Text = Text[len(Prefix):].strip()
			if Text.endswith(Prefix):
				Text = Text[:-len(Prefix)].strip()
		if Text.startswith("*"):
			Text = Text[1:].strip()
		Out.append(f"{DOC_PREFIX} {Text}".rstrip())
	return Out


def ParseDoc(Lines: list[str]) -> list[tuple[str, str]]:
	"""`///` 块 → [(tag, text)]，**只收带 `@` 的语义标签**。

	文档内容是"注解决定了语义"的那部分：`@file` / `@brief` / `@param` / `@return` / `@tparam` /
	`@note` / `@warning` / `@see`。**没打标的 `///` 裸行不收集**（注释照写，但它不进接口文档）；
	标签的续行（含中间的空 `///`）并入上一条标签，所以多段 `@brief` 仍然是一句说明。
	"""
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
		if Current is not None:                  # 续行 = 上一条标签的一部分
			Tag, Text = Tags[Current]
			Tags[Current] = (Tag, (Text + " " + Body).strip())
	return Tags


def DescOf(Tags: list[tuple[str, str]]) -> list[str]:
	"""说明行的渲染顺序：`@brief` 在前，`@param`/`@tparam`/`@return` 依源码顺序在后。

	标签一律渲染成**英文**（`Name: …` / `Returns: …` / `Note: …`）—— 注释本身可能是任何语言，
	标签是结构，不该两处混排。"""
	Out: list[str] = []
	Head = " ".join(Text for Tag, Text in Tags if Tag in ("brief", "file") and Text).strip()
	if Head:
		Out.append(Head)
	for Tag, Text in Tags:
		if not Text or Tag in ("brief", "file"):
			continue
		if Tag in ("param", "tparam"):
			Name, _, Rest = Text.partition(" ")
			Out.append(f"`{Name}`: {Rest.strip()}" if Rest.strip() else f"`{Name}`")
		elif Tag in ("return", "returns"):
			Out.append(f"Returns: {Text}")
		elif Tag == "note":
			Out.append(f"Note: {Text}")
		elif Tag == "warning":
			Out.append(f"Warning: {Text}")
		elif Tag == "see":
			Out.append(f"See: {Text}")
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

NAME_TYPES = ("identifier", "type_identifier", "field_identifier", "qualified_identifier",
              "template_type", "destructor_name", "operator_name")
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


SIGNATURE_MACRO_RE = re.compile(r"^[A-Z][A-Z0-9_]*\s*\(")


def IsMacroCallSignature(Signature: str) -> bool:
	"""`MAHO_DECLARE_FRAME(FLog);` 这类**纯宏调用行**的兜底判据（签名形态：全大写标识符紧跟左括号）。

	正常情况下宏标记在解析前就被剥成空气了（见 StripMacroMarkers），所以走到这里的只剩"没被剥干净"的
	形态（例如括号没配平）；它仍不该被要求写 `@brief`。"""
	return bool(SIGNATURE_MACRO_RE.match(Signature.strip()))


def HasFunctionDeclarator(Node: FSnap) -> bool:
	"""这个成员是不是"函数"：**要在声明符链里递归找** `function_declarator`。

	只看直接子节点会漏掉被包了一层的形态 —— `void* GetProcAddress(...)` 的直接子节点是
	`pointer_declarator`，`std::string& GetChildren(...)` 与 `operator=(...)` 是 `reference_declarator`，
	它们里面才藏着 `function_declarator`（漏判的表现是"函数被列进字段表"）。"""
	Stack = list(Node.Kids)
	while Stack:
		Current = Stack.pop()
		if Current.Type in BODY_TYPES:          # 函数体/类体不是声明符，别进去
			continue
		if Current.Type == "function_declarator":
			return True
		Stack.extend(Current.Kids)
	return False


FORWARD_DECL_RE = re.compile(r"^(struct|class|union|enum)\s+\w+\s*;$")


def IsForwardDeclaration(Signature: str) -> bool:
	"""`struct FImpl;` 这类前向声明：它是**嵌套类型**，不是字段（字段表里出现它，读的人会以为
	那是个数据成员）。"""
	return bool(FORWARD_DECL_RE.match(Signature.strip()))


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


def InnerDeclOf(Node: FSnap):
	"""`template <...>` 里真正被声明的东西：**最后一个"声明类"子节点**。

	不能取"最后一个子节点"：模板声明的子节点里带着 `template` / `<` / `;` 这些**标点与关键字**，
	`template <typename T> class X {...};` 的最后一个子节点就是那个 `;`（取它等于什么都没声明 ——
	实测 `Delegate.h` 的 `TMulticastEvent` 因此整个类被跳过，检查报"0 个成员"）。"""
	Inner = [C for C in Node.Kids if C.Type in DECLARATION_TYPES]
	return Inner[-1] if Inner else None


def Matches(Rel: str, Options) -> bool:
	"""`--filter` 命中判断：逗号分隔可给多个子串（"只收这些头"是常态 —— 未封板的头不该进文档）。"""
	Needles = [Part for Part in Options.filter.split(",") if Part]
	return not Needles or any(Part in Rel for Part in Needles)


class FMacroDef:
	"""一条 `#define`，作为**可展示的宏实体**收集下来（`#define` 的宏体在解析前被抹成空气，
	所以宏只能这样进文档：从源码扫出名字、参数表和 `///` 注释）。"""

	__slots__ = ("Row", "Name", "Params", "Tags")

	def __init__(self, Row: int, Name: str, Params: str, Tags: list[tuple[str, str]]) -> None:
		self.Row = Row
		self.Name = Name
		self.Params = Params
		self.Tags = Tags


def CollectMacroDefs(Rows: list[str]) -> list[FMacroDef]:
	"""扫出所有 `#define`（含多行定义），连同它们**紧贴上方**的 `///` 注释块。"""
	Defines = []
	Seen: set[str] = set()
	Index = 0
	while Index < len(Rows):
		Line = Rows[Index].lstrip()
		if not (Line.startswith("#") and Line[1:].lstrip().startswith("define")):
			Index += 1
			continue
		Body = Line[1:].lstrip()[len("define"):].strip()
		Match = re.match(r"([A-Za-z_]\w*)\s*(\(([^)]*)\))?", Body)
		if Match is None:
			Index += 1
			continue
		Name = Match.group(1)
		# 同名只收第一次（`#if` 各分支里会重复定义同一个名字，收三遍只会让文档重复三行）。
		if Name in Seen:
			while Rows[Index].rstrip().endswith("\\") and Index + 1 < len(Rows):
				Index += 1
			Index += 1
			continue
		Seen.add(Name)
		Tags = ParseDoc(DocLines(FSnap(Name, Index, 0, 0), Rows))
		Defines.append(FMacroDef(Index, Name, Match.group(3) or "", Tags))
		while Rows[Index].rstrip().endswith("\\") and Index + 1 < len(Rows):
			Index += 1
		Index += 1
	return Defines


class FScanner:
	def __init__(self, Root: Path, Options) -> None:
		self.Root = Root
		self.Options = Options
		self.Parser = FParser_()
		self.Stats = {"headers": 0, "entities": 0, "members": 0, "documented": 0, "missing": 0}
		self.Problems: list[str] = []
		self.PendingMacros: list[FMacroDef] = []
		self.Rel = ""
		self.bFunctionsOpen = False

	def Report(self, Rel: str, Node: FSnap, Message: str) -> None:
		self.Problems.append(f"{Rel}:{Node.Row + 1}: {Message}")

	def FlushMacros(self, Row: int) -> None:
		"""把 `Row` 之前（源码顺序）的宏实体先发出去 —— 宏在解析前就被抹成空气，只能这样进文档，
		而且必须**按源码位置**穿插，否则 `///` 注释会挂到错误的实体上。"""
		while self.PendingMacros and self.PendingMacros[0].Row < Row:
			Macro = self.PendingMacros.pop(0)
			D.Macro(Macro.Name, f"({Macro.Params})" if Macro.Params else "", DescOf(Macro.Tags))
			self.Stats["entities"] += 1
			if not any(Tag == "brief" and Text for Tag, Text in Macro.Tags):
				self.Problems.append(f"{self.Rel}:{Macro.Row + 1}: 缺 @brief：{Macro.Name}")

	def Headers(self) -> list[Path]:
		Out = []
		for Path_ in sorted(self.Root.rglob("*.h")):
			if any(Part in {".vs", "Intermediate", "Binaries", "Packaged", "x64"}
			       for Part in Path_.parts):
				continue
			Rel = Path_.relative_to(self.Root).as_posix()
			if not Matches(Rel, self.Options):
				continue
			Out.append(Path_)
		return Out

	def ScanHeader(self, Path_: Path) -> None:
		Rel = Path_.relative_to(self.Root).as_posix()
		self.Rel = Rel                              # 宏实体在 FlushMacros 里上报，需要它
		if self.Options.verbose:
			print(f"[docs_scan] 扫描 {Rel}", flush=True)
		Raw = Path_.read_bytes()
		# 行表取自**未剥离**的源码：宏定义的收集必须看得见 `#define` 原文（多行宏体在解析前已被抹成
		# 空气），而注释与行号在两个版本里一致。
		Rows = Raw.decode("utf-8", "replace").split("\n")
		Source, Tree = self.Parser.Parse(Raw)
		Root = Snapshot(Tree, Source)

		ErrorRow = FirstError(Root)
		if ErrorRow >= 0:
			# 作为一条"问题"上报，而不是打印：隔离模式里子进程的 stdout 只取最后一行（JSON），
			# 打印会静默丢掉 —— 解析 ERROR 必须是能进 --check 清单的东西。
			self.Report(Rel, Root, f"解析有 ERROR（第 {ErrorRow + 1} 行起）—— 该处及之后的声明可能读不到")
			self.Stats["parse_errors"] = self.Stats.get("parse_errors", 0) + 1

		FileLines = FileDoc(Rows)
		FileTags = ParseDoc(FileLines)
		D.Header(Rel, Desc=DescOf(FileTags))
		if not FileLines and not self.Options.quiet:
			self.Report(Rel, Root, "缺 @file：这个头没有文件级说明（`/// @file` 块）")
		self.DeclareIncludes(Rows)
		self.Stats["headers"] += 1
		self.bFunctionsOpen = False          # 「自由函数」一个头只开一次（不是一个函数一个）
		self.PendingMacros = CollectMacroDefs(Rows)
		self.Walk(Root, Source, Rows, Rel)
		self.FlushMacros(1 << 30)            # 文件末尾剩下的宏

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
	            Template: str = "", Doc: list[tuple[str, str]] | None = None) -> None:
		self.FlushMacros(Node.Row)
		if Node.Type in TEMPLATE_TYPES:
			Inner = InnerDeclOf(Node)
			if Inner is None:
				return
			self.Declare(Inner, Source, Rows, Rel, TemplatePrefix(Node, Source),
			             Doc if Doc is not None else ParseDoc(DocLines(Node, Rows)))
			return
		if Node.Type in ("comment", "preproc_include"):
			return

		Tags = Doc if Doc is not None else ParseDoc(DocLines(Node, Rows))
		Desc = DescOf(Tags)
		Documented = bool(Desc)

		if Node.Type in ("class_specifier", "struct_specifier", "union_specifier"):
			if Node.Find("field_declaration_list") is None:
				return                       # 顶层前向声明：没有 API 可展示（不是这个类型的家）
			self.DeclareClass(Node, Source, Rows, Rel, Template, Desc)
		elif Node.Type == "enum_specifier":
			Name = Node.Find(*NAME_TYPES)
			if Name is None:
				return
			D.Enum(Name.Text(Source), Base="", Desc=Desc)
			self.Stats["entities"] += 1
		elif Node.Type == "alias_declaration":
			Name = Node.Find(*NAME_TYPES)
			# 取**最后一个**值形态的子节点：`using X = Y;` 的名字本身也是 `type_identifier`，
			# 取第一个会得到"X : X"。
			Values = Node.All(*VALUE_TYPES)
			D.Alias(Name.Text(Source) if Name is not None else "?",
			        Values[-1].Text(Source) if Values else "", Desc=Desc)
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
		# 渲染层自己会补 ": " 前缀 —— 子句自己的冒号要去掉，否则出来是 ": : public X"。
		BaseText = Base.Text(Source).lstrip(":").strip() if Base is not None else ""
		Kind((Template + Name.Text(Source)).strip(), Base=BaseText, Desc=Desc)
		self.Stats["entities"] += 1

		Body = Node.Find("field_declaration_list")
		if Body is None:
			return
		D.SetAccess("private" if Node.Type == "class_specifier" else "public")
		for Member in Body.Kids:
			if Member.Type == "access_specifier":
				D.SetAccess(Member.Text(Source).rstrip(":"))
				continue
			# 成员**不**继承类的模板前缀：类的 `template <...>` 已经写在类名那行了，成员只在它自己
			# 是成员模板时才带 `template <...>`（那是 DeclareMember 自己的事）。
			self.DeclareMember(Member, Source, Rows, Rel, "")

	def DeclareMember(self, Node: FSnap, Source: bytes, Rows: list[str], Rel: str,
	                  Template: str = "", Doc: list[tuple[str, str]] | None = None) -> None:
		self.FlushMacros(Node.Row)
		if Node.Type == "comment":
			return
		if Node.Type in TEMPLATE_TYPES:
			Inner = InnerDeclOf(Node)
			if Inner is None:
				return
			self.DeclareMember(Inner, Source, Rows, Rel,
			                   Template + TemplatePrefix(Node, Source),
			                   Doc if Doc is not None else ParseDoc(DocLines(Node, Rows)))
			return

		Tags = Doc if Doc is not None else ParseDoc(DocLines(Node, Rows))
		Desc = DescOf(Tags)
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
		if IsForwardDeclaration(Signature):      # 前向声明是嵌套类型，不是字段
			D.Nested(Signature.rstrip(";").split()[-1], Type=Signature.split()[0], Desc=Desc)
			self.Stats["members"] += 1
			return
		if Node.Type == "function_definition" or HasFunctionDeclarator(Node):
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
		if not Matches(Rel, Options):
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
		if not Options.quiet:
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
