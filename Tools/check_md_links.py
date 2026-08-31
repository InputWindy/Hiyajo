#!/usr/bin/env python3
"""Maho 文档链接完整性检查：遍历仓库内所有 .md，校验相对链接目标存在。
跳过：代码块/行内代码中的假链接、AGENTS.md（既有 agent 约定文件）。
用法: python check_md_links.py [repo_root]
"""
import os
import re
import sys

try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

ROOT = sys.argv[1] if len(sys.argv) > 1 else "."
SKIP_DIRS = {"_deps", ".git", ".vs", "Build", "Packaged", "x64", "__pycache__"}
LINK_RE = re.compile(r"\[[^\]]*\]\(([^)]+)\)")
FENCE_RE = re.compile(r"```.*?```", re.DOTALL)
INLINE_CODE_RE = re.compile(r"`[^`]*`")

def is_skipped_dir(path):
    parts = set(path.replace("\\", "/").split("/"))
    return bool(parts & SKIP_DIRS)

def strip_code(text):
    text = FENCE_RE.sub("", text)          # 去 fenced code block
    text = INLINE_CODE_RE.sub("", text)     # 去行内 code span
    return text

def main():
    broken = []
    total_links = 0
    checked_files = 0
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if not is_skipped_dir(d)]
        if is_skipped_dir(dirpath):
            continue
        for fn in filenames:
            if not fn.endswith(".md"):
                continue
            if fn == "AGENTS.md":           # 既有 agent 约定文件，不在重写范围
                continue
            path = os.path.join(dirpath, fn)
            checked_files += 1
            base = os.path.dirname(path)
            try:
                text = open(path, encoding="utf-8").read()
            except Exception as e:
                broken.append(f"{path}: <unreadable {e}>")
                continue
            text = strip_code(text)
            for m in LINK_RE.finditer(text):
                target = m.group(1).strip()
                if not target or target.startswith(("http://", "https://", "#", "mailto:")):
                    continue
                file_part = target.split("#", 1)[0]
                if not file_part:
                    continue
                full = os.path.normpath(os.path.join(base, file_part))
                total_links += 1
                if not os.path.exists(full):
                    broken.append(f"{path}: -> {target} (missing {full})")
    print(f"检查 {checked_files} 个 .md，共 {total_links} 个文件链接")
    if broken:
        print(f"\n发现 {len(broken)} 个坏链接:")
        for b in broken:
            print("  " + b)
        return 1
    print("全部链接有效 [OK]")
    return 0

if __name__ == "__main__":
    sys.exit(main())
