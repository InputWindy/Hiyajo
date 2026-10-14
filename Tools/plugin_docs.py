#!/usr/bin/env python3
# Run via Tools/maho_python.bat — engine Tools/python only.
"""
plugin_docs.py —— 为每个插件生成 `<plugin>/Docs.html`（与 `.cplugin` 同级）

**与 Source/Docs.html 同一套来源**：直接调用 `Tools/docs_scan.py --root <plugin>` —— 扫描该插件的
头文件、从 `///` 注释（`@file` / `@brief` / `@param` / `@return` / `@tparam`）提取实体与成员。
所以插件文档也是"改注释 = 改文档"，不再需要每个插件一份手写的 `Docs.py`（已退役）。

用法：
  Tools\\maho_python.bat Tools\\plugin_docs.py                # Plugins/ 下所有 .cplugin
  Tools\\maho_python.bat Tools\\plugin_docs.py --filter Log   # 只做名字含 Log 的
  Tools\\maho_python.bat Tools\\plugin_docs.py --check        # 只审注释（有问题退 1）
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def PluginDirs(Root: Path) -> list[Path]:
	"""含有 `.cplugin` 的目录 = 一个插件（它的 .cplugin 就是"同级"那个参照物）。"""
	return sorted(Path(P).parent for P in Root.rglob("*.cplugin"))


def main() -> int:
	Parser = argparse.ArgumentParser()
	Parser.add_argument("--plugins", default="Plugins")
	Parser.add_argument("--filter", default="", help="只处理路径里含该子串的插件")
	Parser.add_argument("--check", action="store_true", help="只审注释（不写 HTML）")
	Parser.add_argument("--verbose", action="store_true", help="--check 时逐条打印问题")
	Args = Parser.parse_args()

	Root = Path(Args.plugins)
	if not Root.is_dir():
		print(f"[plugin_docs] 目录不存在：{Root}")
		return 1

	Dirs = [P for P in PluginDirs(Root) if Args.filter.lower() in P.as_posix().lower()]
	Failed = 0
	for Dir in Dirs:
		# 一个插件一个进程：扫描器内部还会按头文件再分子进程（见 docs_scan.py 的说明）。
		Command = [sys.executable, str(HERE / "docs_scan.py"), "--root", str(Dir),
		           "--out", str(Dir / "Docs.html")]
		if Args.check:
			Command += ["--check", "--quiet"]
		Proc = subprocess.run(Command, capture_output=True, text=True, encoding="utf-8",
		                      errors="replace")
		Lines = (Proc.stdout or "").strip().splitlines()
		print(f"[plugin_docs] {Dir.as_posix()}/Docs.html  {Lines[-1] if Lines else '（无输出）'}")
		if Args.check and Args.verbose and Proc.returncode == 1:
			for Problem in Lines[:-1]:
				print(f"    {Problem}")
		# --check 下 1 = "有问题"（这正是这一趟要报的），只有别的退出码/没输出才算扫描失败。
		if (Args.check and Proc.returncode not in (0, 1)) or (not Args.check and Proc.returncode != 0) \
			or not Lines:
			Failed += 1

	print(f"[plugin_docs] {len(Dirs)} 个插件 · 失败 {Failed} 个")
	return 1 if Failed else 0


if __name__ == "__main__":
	raise SystemExit(main())
