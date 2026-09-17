#!/usr/bin/env python3
# Run via Tools/maho_python.bat — engine Tools/python only.
"""
plugin_docs.py —— 为每个插件生成 `<plugin>/Docs.html`（与 `.cplugin` 同级）

只做一件事：**扫描该插件的头文件结构**（左树），右侧**留白** —— 内容以后逐条声明
（与 Source/Docs.html 同一套原子接口，见 Tools/docs_builder.py）。

用法：
  Tools\\maho_python.bat Tools\\plugin_docs.py                # Plugins/ 下所有 .cplugin
  Tools\\maho_python.bat Tools\\plugin_docs.py --filter Log   # 只做名字含 Log 的
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import docs_builder as D


def PluginDirs(Root: Path) -> list[Path]:
	"""含有 `.cplugin` 的目录 = 一个插件（它的 .cplugin 就是"同级"那个参照物）。"""
	return sorted(Path(P).parent for P in Root.rglob("*.cplugin"))


def main() -> int:
	Parser = argparse.ArgumentParser()
	Parser.add_argument("--plugins", default="Plugins")
	Parser.add_argument("--filter", default="", help="只处理路径里含该子串的插件")
	Args = Parser.parse_args()

	Root = Path(Args.plugins)
	if not Root.is_dir():
		print(f"[plugin_docs] 目录不存在：{Root}")
		return 1

	Dirs = [P for P in PluginDirs(Root) if Args.filter.lower() in P.as_posix().lower()]
	Count = 0
	WithContent = 0
	TotalHeaders = 0
	for Dir in Dirs:
		D.Reset()                       # 每个插件一份声明集
		# 插件自己的内容脚本（可选）：<plugin>/Docs.py，用与 Source 相同的原子接口。
		# 只声明、不需要 import —— 运行器把 docs_builder 作为 D 注入。
		ContentScript = Dir / "Docs.py"
		if ContentScript.is_file():
			try:
				Code = compile(ContentScript.read_text(encoding="utf-8"), str(ContentScript), "exec")
				exec(Code, {"__file__": str(ContentScript), "__name__": "plugin_docs_content", "D": D})
				WithContent += 1
			except Exception as Error:   # 一个插件的内容坏了，不影响其它插件
				print(f"[plugin_docs] {Dir.as_posix()}/Docs.py 出错（只留树）："
				      f"{type(Error).__name__}: {Error}")
				D.Reset()
		Headers, Declared, Members = D.Build(Out=Dir / "Docs.html", Root=Dir)
		TotalHeaders += Headers
		Count += 1
		print(f"[plugin_docs] {Dir.as_posix()}/Docs.html   {Headers} 个头 · 已声明 {Declared} · {Members} 个成员")

	print(f"[plugin_docs] {Count} 个插件 · {TotalHeaders} 个头文件 · {WithContent} 个带内容脚本")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
