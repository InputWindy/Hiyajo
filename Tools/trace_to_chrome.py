#!/usr/bin/env python3
"""Convert a Maho CPU trace into Chrome Trace Event Format.

Input  : Profile_trace.txt -- the "[tr] ..." lines the engine writes when MAHO_TRACE is set (see
         Source/Public/Core/Profiler.h). Two shapes, distinguished by the grp= field:

             [tr] ... lane=<Group> slot=<n> worker=<k> owner=<Frame> name=<Class>::<Function> tip=<text>
             [tr] ... lane=<Group> slot=<n> worker=<k> owner=<Frame> grp=<Group> name=<Frame>::<Stage> ...
             [tr] ... lane=Global  ... (the same node, mirrored; the mirror keeps its ORIGIN group)

Output : a JSON file that chrome://tracing and https://ui.perfetto.dev load directly.

HOW THE ROWS ARE BUILT:

  * `lane` (the GROUP: "Engine", "Render", "RHIServer") becomes the Perfetto PROCESS: one collapsible
    section per architectural partition, so the left panel reads as the architecture.
  * `owner` (the frame that was running, or a resident thread's role name) becomes the TRACK inside it,
    so a partition's rows are the things that make it up: FScene, FDrawTriangleFeature, FUIFeature, ...

  That pairing is what a timeline can actually draw. A track may only NEST bars or SEQUENCE them --
  never overlap them -- and any offence is reported as an import error and moved onto a spill track.
  Per FRAME is safe because the engine chains a frame's stages in order (and each stage against its
  own previous frame: the next frame's `IBeginRender` waits for this frame's), so the bars of one
  (group, owner) nest by construction. Per GROUP alone is NOT safe: two independent features of one
  frame really do run at the same time, and no row can show that.

  `slot` (in-flight ring slot) and `worker` (which thread ran it) travel in args: they answer "which
  pipeline position" and "who ran it" on hover, without deciding layout.

Event names keep the owner: a node bar is `<Frame>::<Stage>`, a manual scope is `Class::Function`.

Usage:
    maho_python.bat Tools/trace_to_chrome.py [input.txt] [output.json] [--no-global]

With no arguments it reads ./Profile_trace.txt (the working directory the engine runs in) and
writes the matching .json beside it.
"""

import json
import pathlib
import re
import sys

# -- the current format -------------------------------------------------------------------------
NEW_NODE = re.compile(
    r"^\[tr\] ts=(\d+) dur=(\d+) lane=(\S+)(?: slot=(\d+))?(?: worker=(\d+))?(?: owner=(\S+))?"
    r" grp=(\S*) name=(.+?)(?: tip=(.*))?$")
NEW_SCOPE = re.compile(
    r"^\[tr\] ts=(\d+) dur=(\d+) lane=(\S+)(?: slot=(\d+))?(?: worker=(\d+))?(?: owner=(\S+))?"
    r"(?: func=(\S+))? name=(.+?)(?: tip=(.*))?$")

# -- synchronization points: a flow from the gate's row to the row it released -------------------
FLOW = re.compile(
    r"^\[tr\] flow a_ts=(\d+) a_lane=(\S+)(?: a_slot=(\d+))?(?: a_owner=(\S+))?(?: a_name=(\S+))?"
    r" b_ts=(\d+) b_lane=(\S+)(?: b_slot=(\d+))?(?: b_owner=(\S+))?(?: b_name=(\S+))?$")

# -- files from before the group rework: a numeric lane id, and the frame name in the event -------
OLD_NODE = re.compile(r"^\[tr\] ts=(\d+) dur=(\d+) tid=(\d+) grp=(\S*) name=(.+)$")
OLD_SCOPE = re.compile(r"^\[tr\] ts=(\d+) dur=(\d+) tid=(\d+) name=(.+)$")

# The fold section for events that belong to no group of their own.
ROOT_NAME = "Maho"
ROOT = 1

# The reserved group every graph node is mirrored onto.
GLOBAL = "Global"


def convert(src: pathlib.Path, dst: pathlib.Path, drop_global: bool,
            drops: set | None = None, spacer: bool = True,
            with_flows: bool = True) -> tuple[int, int, int, int]:
    # Every event, normalized to:
    #   (ts, dur, group, owner, slot, worker, is_node, name, tip, origin)
    records = []
    flows = []      # (a_ts, a_group, a_owner, b_ts, b_group, b_owner)
    # Old files only: which frame a numeric lane belonged to (derived from the node events).
    old_lane_frame = {}

    with src.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            text = line.strip()
            if (m := FLOW.match(text)) is not None:
                flows.append((int(m.group(1)), m.group(2), m.group(4) or m.group(2), m.group(5),
                              int(m.group(6)), m.group(7), m.group(9) or m.group(7), m.group(10)))
                continue
            if (m := NEW_NODE.match(text)) is not None:
                lane, owner = m.group(3), m.group(6) or ""
                records.append((int(m.group(1)), int(m.group(2)), lane, owner, int(m.group(4) or -1),
                                int(m.group(5) or -1), True, m.group(8), m.group(9) or "",
                                m.group(7) or ROOT_NAME, ""))
            elif (m := NEW_SCOPE.match(text)) is not None:
                # A scope carries no group of its own: it ran inside whatever the lane already was,
                # and it inherits that node's owner and slot too, which is what puts it on the same row.
                # `func=` appears when the bar is named by a hand-written section instead.
                lane = m.group(3)
                records.append((int(m.group(1)), int(m.group(2)), lane, m.group(6) or "", int(m.group(4) or -1),
                                int(m.group(5) or -1), False, m.group(8), m.group(9) or "", lane,
                                m.group(7) or ""))
            elif (m := OLD_NODE.match(text)) is not None:
                frame, _, stage = m.group(5).partition("::")
                old_lane_frame[m.group(3)] = frame
                records.append((int(m.group(1)), int(m.group(2)), m.group(4) or ROOT_NAME, frame, -1,
                                -1, True, m.group(5), "", m.group(4) or ROOT_NAME, ""))
            elif (m := OLD_SCOPE.match(text)) is not None:
                records.append((int(m.group(1)), int(m.group(2)), "", m.group(4), -1, -1, False,
                                m.group(4), "", "", ""))
            else:
                continue

    # A manual scope in an old file: give it the frame of the lane it landed on, so it stays on the
    # same row it used to.
    resolved = []
    for ts, dur, group, owner, slot, worker, is_node, name, tip, origin, func in records:
        if not is_node and not group:
            owner = old_lane_frame.get(owner, owner)
            group = ROOT_NAME
            origin = ROOT_NAME
        resolved.append((ts, dur, group, owner, slot, worker, is_node, name, tip, origin, func))
    records = resolved

    if drop_global:
        records = [r for r in records if r[2] != GLOBAL]

    # Whole groups can be filtered out by name: a section nobody reads is noise, and (for a group
    # whose bars come from more than one thread, e.g. a scheduler that is also called by workers) it
    # is the one place a single track cannot be serial.
    if drops:
        records = [r for r in records if r[2] not in drops]

    # One Perfetto process per GROUP: the fold section, i.e. the architecture.
    group_pid = {ROOT_NAME: ROOT}
    for group in sorted({r[2] for r in records if r[2] and r[2] != ROOT_NAME}):
        group_pid[group] = len(group_pid) + 1

    # One track per (group, owner); groups by first event, owners by first event as well, so the
    # layout is stable and needs no separate ordering data. An event with no owner (an old file)
    # falls back to the group name.
    first_ts: dict[tuple[str, str], int] = {}
    for record in records:
        ts, group, owner = record[0], record[2], record[3]
        key = (group, owner or group)
        first_ts[key] = min(first_ts.get(key, ts), ts)

    track: dict[tuple[str, str], tuple[int, int]] = {}
    for group in sorted(group_pid):
        rows = sorted((owner for g, owner in first_ts if g == group),
                      key=lambda owner: (first_ts[(group, owner)], owner))
        for index, owner in enumerate(rows):
            track[(group, owner)] = (group_pid[group], index + 1)

    events = []

    def metadata(name: str, pid: int, tid: int | None, args: dict) -> None:
        entry = {"name": name, "ph": "M", "pid": pid, "args": args}
        if tid is not None:
            entry["tid"] = tid
        events.append(entry)

    metadata("process_name", ROOT, None, {"name": ROOT_NAME})
    metadata("process_sort_index", ROOT, None, {"sort_index": 0})
    for group, pid in group_pid.items():
        if group == ROOT_NAME:
            continue
        metadata("process_name", pid, None, {"name": group})
        metadata("process_sort_index", pid, None, {"sort_index": pid})

    for (group, owner), (pid, tid) in track.items():
        # The row is named by its OWNER inside its group's section, so "render" reads as the list of
        # things that make it up rather than as a wall of unrelated bars.
        metadata("thread_name", pid, tid, {"name": owner})
        metadata("thread_sort_index", pid, tid, {"sort_index": tid})

    for ts, dur, group, owner, slot, worker, is_node, name, tip, origin, func in records:
        pid, tid = track[(group, owner or group)]
        if is_node:
            # A node bar keeps its full `<Frame>::<Stage>` name; the halves go to args as well, where
            # a hover shows them with the pipeline slot and the partition it belongs to.
            frame, _, stage = name.partition("::")
            args = {"frame": frame, "stage": stage, "group": origin}
            if tip:
                args["tip"] = tip
            if slot >= 0:
                args["slot"] = slot
            if worker >= 0:
                args["worker"] = worker
            events.append({
                "name": name, "cat": "graph", "ph": "X", "ts": ts, "dur": dur,
                "pid": pid, "tid": tid, "args": args,
            })
        else:
            args = {"group": origin}
            if func:
                # The bar is named by a hand-written SECTION, so this is where it actually lives --
                # the one thing the old `__FUNCTION__` naming gave for free.
                args["func"] = func
            if tip:
                args["tip"] = tip
            if slot >= 0:
                args["slot"] = slot
            if worker >= 0:
                args["worker"] = worker
            events.append({
                "name": name, "cat": "cpu", "ph": "X", "ts": ts, "dur": dur,
                "pid": pid, "tid": tid, "args": args,
            })

    # -- tie-break pass, per track ------------------------------------------------------------------
    # Perfetto orders slices STRICTLY: two bars that start in the SAME microsecond cannot be nested,
    # so a genuinely nested pair -- a parent bar and the child scope it opened within the same
    # microsecond, which is all the resolution the trace has -- is reported as a partial overlap and
    # moved onto a spill track. That is the last remaining import error, and it is cosmetic: among
    # the bars sharing a timestamp the WIDEST one is the parent, so the rest are nudged 1us later and
    # clamped inside it. Invisible at display scale; the structure is exactly what the engine said.
    per_track: dict[tuple[int, int], list] = {}
    for e in events:
        if e["ph"] == "X":
            per_track.setdefault((e["pid"], e["tid"]), []).append(e)

    for bars in per_track.values():
        # Repeat until nothing ties: one pass only separates the outermost pair, and three-deep
        # nesting (a node bar, the scope inside it, the scope inside THAT -- all within a couple of
        # microseconds) needs another. Bounded, because each pass can only move bars later.
        for _ in range(8):
            bars.sort(key=lambda e: (e["ts"], -e["dur"]))
            moved = False
            index = 0
            while index < len(bars):
                last_tie = index
                while last_tie + 1 < len(bars) and bars[last_tie + 1]["ts"] == bars[index]["ts"]:
                    last_tie += 1
                if last_tie > index:
                    moved = True
                    parent = bars[index]
                    parent_end = parent["ts"] + parent["dur"]
                    for position in range(index + 1, last_tie + 1):
                        other = bars[position]
                        other["ts"] = parent["ts"] + 1
                        # Inside the parent AND clear of whatever follows: a nudge that merely moves
                        # the bar one microsecond later must not make it touch its successor.
                        limit = parent_end
                        if position + 1 < len(bars):
                            limit = min(limit, bars[position + 1]["ts"])
                        else:
                            limit = min(limit, other["ts"] + other["dur"])
                        other["dur"] = max(1, limit - other["ts"])
                index = last_tie + 1
            if not moved:
                break

    # -- spacer pass (DEFAULT ON; --no-spacer keeps the raw timing) ---------------------------------
    # A partial overlap (A starts first, B starts inside A and ends after it) cannot be drawn, so
    # Perfetto reports it and spills B. The repair is the least destructive one available: A's END is
    # pulled back to 1us before B's start, leaving a one-microsecond partition between them. Starts are
    # never moved, so nothing cascades and the frame structure stays where the engine put it; what
    # changes is A's duration, by exactly the amount it used to overlap -- which is why it can be
    # switched off when the raw timing matters more than a clean import.
    if spacer:
        for bars in per_track.values():
            bars.sort(key=lambda e: e["ts"])
            for index, first in enumerate(bars):
                first_end = first["ts"] + first["dur"]
                trimmed = first_end
                for other in bars[index + 1:]:
                    if other["ts"] >= first_end:
                        break
                    if other["ts"] + other["dur"] > first_end and other["ts"] > first["ts"]:
                        trimmed = min(trimmed, other["ts"] - 1)
                if trimmed < first_end:
                    first["dur"] = max(1, trimmed - first["ts"])

    # -- synchronization points as FLOW arrows ------------------------------------------------------
    # Both ends are BOUND TO THEIR BARS, which is why the line carries their names: a viewer draws a
    # flow only between slices it can bind to, and timestamps alone are not enough -- the gate's end is
    # a hair BEFORE its bar closes, and the released node is announced a few microseconds BEFORE its
    # own bar opens. So the arrow starts inside the gate's bar (1us before its end) and finishes inside
    # the released bar (at its start).
    def BindBar(row, name, ts, b_before):
        bars = per_track.get(row)
        if not bars or not name:
            return None
        best = None
        for bar in bars:
            if bar["name"] != name:
                continue
            if (bar["ts"] <= ts) if b_before else (bar["ts"] >= ts):
                if best is None or (abs(bar["ts"] - ts) < abs(best["ts"] - ts)):
                    best = bar
        return best

    drawn_flows = 0
    for index, flow in enumerate(flows if with_flows else []):
        a_ts, a_group, a_owner, a_name, b_ts, b_group, b_owner, b_name = flow
        a_row = track.get((a_group, a_owner))
        b_row = track.get((b_group, b_owner))
        if a_row is None or b_row is None:
            continue   # an endpoint's section was dropped (--drop / --no-global)
        gate = BindBar(a_row, a_name, a_ts, True)
        released = BindBar(b_row, b_name, b_ts, False)
        if gate is None or released is None:
            continue
        events.append({"name": f"{a_name} -> {b_name}" if a_name and b_name else "sync",
                       "cat": "sync", "ph": "s", "id": index + 1,
                       # Both ends sit at the START of the bar they belong to, which is the one instant
                       # that bar is certainly the INNERMOST slice there (the tie-break pass above keeps
                       # children off their parent's first microsecond). A viewer attaches a flow to the
                       # slice under the point, so this is what makes the arrow read stage -> stage
                       # instead of hooking onto whatever hand-written scope happened to be innermost at
                       # the gate's last microsecond.
                       "ts": gate["ts"], "pid": a_row[0], "tid": a_row[1]})
        events.append({"name": "sync", "cat": "sync", "ph": "f", "id": index + 1,
                       "ts": released["ts"], "pid": b_row[0], "tid": b_row[1]})
        drawn_flows += 1

    payload = {"traceEvents": events, "displayTimeUnit": "ms"}
    dst.write_text(json.dumps(payload), encoding="utf-8")
    return len(events), len(group_pid), len(track), drawn_flows


def main() -> int:
    drop_global = "--no-global" in sys.argv
    # The spacer pass is ON by default: a trace that imports without errors is worth more than the
    # last microsecond of a bar that overlapped the next one. --no-spacer restores the raw timing.
    spacer = "--no-spacer" not in sys.argv
    with_flows = "--no-flows" not in sys.argv
    drops: set = set()
    for arg in sys.argv[1:]:
        if arg.startswith("--drop="):
            drops.update(name for name in arg[len("--drop="):].split(",") if name)

    argv = [a for a in sys.argv[1:] if not a.startswith("--")]

    src = pathlib.Path(argv[0]) if len(argv) > 0 else pathlib.Path("Profile_trace.txt")
    dst = pathlib.Path(argv[1]) if len(argv) > 1 else src.with_suffix(".json")

    if not src.is_file():
        print(f"[trace] no such trace: {src}")
        return 1

    count, groups, lanes, flows_drawn = convert(src, dst, drop_global, drops, spacer, with_flows)
    if count == 0:
        print(f"[trace] {src} had no [tr] lines -- was MAHO_TRACE set for that run?")
        return 1

    skipped = "" if spacer else ", raw timing (no spacer)"
    if drop_global:
        skipped += ", Global mirror dropped"
    if drops:
        skipped += f", dropped {','.join(sorted(drops))}"
    print(f"[trace] {count} events ({flows_drawn} flow arrows), {lanes} track(s), "
          f"{groups} group(s){skipped} -> {dst}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
