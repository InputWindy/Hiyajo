# Run before a build — enforces the Maho interface-layering rule:
#
#   * const read  methods → public
#   * non-const write methods → protected (NOT public)
#   * the scheduler's ONLY write entry = friend ExecuteExtension<T, TStage>
#
# A Tool/Layer must never expose a non-const method publicly: another plugin
# could Get() the singleton and write it behind the scheduler's back, breaking
# the thread-safe write model.
#
# Usage:
#   python check_interface_layers.py path\\Game.cproject
#
# Exit 0 = OK, 1 = violation(s) found.

from __future__ import annotations

import os
import re
import sys
from pathlib import Path

os.environ["MAHO_ALLOW_SYSTEM_PYTHON"] = "1"
sys.path.insert(0, str(Path(__file__).resolve().parent))

import maho_tools as m  # noqa: E402


# A method must have a const qualifier to be a legitimate public read.
# Match "RetType Name(args)" / "RetType Name(args) const".
_METHOD_RE = re.compile(
    r"^\s*(?P<ret>[\w:<>&*]+)\s+(?P<name>\w+)\s*\([^;]*\)\s*(?P<qualifiers>const)?\s*;"
)

_SECTION_RE = re.compile(r"^\s*(public|protected|private)\s*:")

_FRIEND_RE = re.compile(
    r"friend\s+bool\s+Maho::ExecuteExtension\s*\(TStage\s+Stage\)"
)

# Any other friend (class / function / FreeService bridge) inside a Tool/Layer is
# a backdoor around the scheduler — must be rejected.
_ANY_FRIEND_RE = re.compile(r"\bfriend\b")

# FStandaloneTag opt-out — a class whose FTags carries it is self-managed and the
# linter skips it. Scan the class body for the marker broadly (the FTags alias may
# nest template args, e.g. FWithTags<typename TTool<X>::FTags, FStandaloneTag>).
_STANDALONE_TAG = "FStandaloneTag"


def _is_tool_or_layer(base: str) -> bool:
    return base.startswith(("TTool<", "TLayer<", "Maho::TTool<", "Maho::TLayer<"))


def _find_class_open(lines: list[str], start: int) -> int | None:
    """Given the line index of the class head, find the '{' that opens the body."""
    depth = 0
    for i in range(start, len(lines)):
        depth += lines[i].count("{")
        depth -= lines[i].count("}")
        if depth > 0 and "{" in lines[i]:
            return i
        if depth < 0:
            return None
    return None


def _find_class_body_end(lines: list[str], open_idx: int) -> int | None:
    depth = 0
    for i in range(open_idx, len(lines)):
        depth += lines[i].count("{")
        depth -= lines[i].count("}")
        if depth == 0 and "}" in lines[i]:
            return i
    return None


def _brace_depth(lines: list[str], open_idx: int, idx: int) -> int:
    depth = 0
    for i in range(open_idx + 1, idx + 1):
        depth += lines[i].count("{")
        depth -= lines[i].count("}")
    # At the class's own open brace we started counting after it; return the
    # current nesting (0 = directly in class body, >0 = inside a nested block).
    return depth


def check_header(path: Path) -> list[str]:
    """Return a list of violation messages for one header file."""
    problems: list[str] = []
    try:
        text = path.read_text(encoding="utf-8-sig")
    except OSError:
        return []

    lines = text.splitlines()

    i = 0
    while i < len(lines):
        # Find a class deriving from TTool<T> / TLayer<T>.
        m = re.search(r"class\s+\w*\s*MACRO\w*\s*\w+\s*:\s*public\s+([\w:<>,]+)", "")
        # Simpler: scan for "class NAME ... : ... TTool<NAME" shapes.
        head = re.match(
            r"^\s*class\s+[A-Za-z_][\w]*\b.*:\s*(public\s+)?.*"
            r"(TTool|TLayer|Maho::TTool|Maho::TLayer)\s*<",
            lines[i],
        )
        if head is None:
            i += 1
            continue

        open_idx = _find_class_open(lines, i)
        if open_idx is None:
            i += 1
            continue
        end_idx = _find_class_body_end(lines, open_idx)
        if end_idx is None:
            i += 1
            continue

        # Walk the class body, tracking access section + nested depth.
        section = "private"
        scheduler_friends = 0
        standalone = False

        for j in range(open_idx + 1, end_idx):
            if _brace_depth(lines, open_idx, j) != 0:
                continue  # inside a nested block (skip)

            sec = _SECTION_RE.match(lines[j])
            if sec:
                section = sec.group(1)
                continue

            # FStandaloneTag opt-out — self-managed class, linter skips it.
            if _STANDALONE_TAG in lines[j]:
                standalone = True

            if standalone:
                # Once marked standalone, stop enforcing the Tool/Layer contract.
                continue

            if _FRIEND_RE.search(lines[j]):
                scheduler_friends += 1
                continue

            if _ANY_FRIEND_RE.search(lines[j].split("//")[0]):
                problems.append(
                    f"{path}:{j + 1}: extra friend (backdoor) — a Tool/Layer may "
                    f"declare ONLY 'template <typename TExtension, typename TStage> "
                    f"friend bool Maho::ExecuteExtension(TStage Stage);'; remove it "
                    f"(external callers go through the scheduler)"
                )
                continue

            mm = _METHOD_RE.search(lines[j].split("//")[0])
            if mm and section == "public":
                if not mm.group("qualifiers"):
                    problems.append(
                        f"{path}:{j + 1}: public write method "
                        f"'{mm.group('name')}' is non-const — move it to protected "
                        f"(only the scheduler may write via ExecuteExtension)"
                    )

        if standalone:
            i = end_idx + 1
            continue

        if scheduler_friends == 0:
            problems.append(
                f"{path}:{i + 1}: Tool/Layer class missing friend "
                f"'template <typename TExtension, typename TStage> "
                f"friend bool Maho::ExecuteExtension(TStage Stage);'"
            )
        elif scheduler_friends > 1:
            problems.append(
                f"{path}:{i + 1}: Tool/Layer class declares "
                f"{scheduler_friends} friend ExecuteExtension — exactly one required"
            )

        i = end_idx + 1

    return problems


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("[ERROR] Usage: check_interface_layers.py path\\Game.cproject", file=sys.stderr)
        return 1

    try:
        cproject = Path(argv[1]).expanduser().resolve()
        data = m.read_cproject(cproject)
        project_name = str(data["ProjectName"])
        engine_root = m.resolve_engine_directory(cproject, data)

        # Every enabled engine/project plugin's Public headers.
        infos = m._all_plugin_infos(engine_root, cproject.parent)
        enabled = {
            p["Name"]
            for p in data.get("Plugins", [])
            if p.get("Enabled", True)
        }

        all_problems: list[str] = []
        for name, info in infos.items():
            if name not in enabled and name != project_name:
                continue
            public_dir = info["public_dir"].replace("${ENGINE_DIR}", str(engine_root))
            public_dir = public_dir.replace("${CMAKE_CURRENT_SOURCE_DIR}", str(cproject.parent))
            for header in Path(public_dir).glob("*.h"):
                all_problems.extend(check_header(header))

        if all_problems:
            print("[Maho] Interface-layering violations:", file=sys.stderr)
            for p in all_problems:
                print(f"  {p}", file=sys.stderr)
            return 1

        print("[Maho] Interface layering OK (write methods protected, friend present)")
        return 0
    except Exception as ex:  # noqa: BLE001
        print(f"[ERROR] {ex}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
