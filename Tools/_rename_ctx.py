"""One-shot: spell the frame context with the NESTED name in implementations.

`FEngineBase::FContext` is an alias of `FEngineContext`, so this is a pure rename -- but the
interface declarations MUST keep the namespace-scope spelling (they are declared before the
scheduler class exists, so the nested name is not nameable there). Those are exactly the pure
virtuals, i.e. the lines ending in `= 0;`.

Usage:  maho_python.bat Tools/_rename_ctx.py          # dry run
        maho_python.bat Tools/_rename_ctx.py --apply
"""

import pathlib
import sys

PAIRS = [
    ("FEngineContext", "FEngineBase"),
    ("FRenderContext", "FRender"),
    ("FGameWorldContext", "FGameWorld"),
    ("FExampleEditorContext", "FExampleEditor"),
]

SKIP_PARTS = {".git", "Intermediate", "x64", "openspec", "Build", "_deps"}


def main() -> int:
    apply = "--apply" in sys.argv
    root = pathlib.Path(".")
    changed, total = [], 0

    for path in root.rglob("*"):
        if path.suffix not in (".h", ".cpp") or not path.is_file():
            continue
        if any(part in SKIP_PARTS for part in path.parts):
            continue

        with path.open("r", encoding="utf-8", newline="") as handle:
            lines = handle.readlines()

        hits = 0
        for index, line in enumerate(lines):
            if "= 0;" in line:                       # an interface declaration: keep as-is
                continue
            for ctx, svc in PAIRS:
                needle = ctx + "&"
                if needle in line:
                    lines[index] = lines[index].replace(needle, svc + "::FContext&")
                    hits += 1

        if hits:
            changed.append((str(path), hits))
            total += hits
            if apply:
                with path.open("w", encoding="utf-8", newline="") as handle:
                    handle.writelines(lines)

    print(("[applied] " if apply else "[dry run] ") + f"{total} replacements in {len(changed)} files")
    for name, hits in sorted(changed, key=lambda item: -item[1]):
        print(f"  {hits:4d}  {name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
