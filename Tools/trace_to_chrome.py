#!/usr/bin/env python3
"""Convert a Maho CPU trace into Chrome Trace Event Format.

Input  : Profile_trace.txt -- the "[tr] ..." lines the engine writes when MAHO_TRACE is set (see
         Source/Public/Core/Profiler.h). Two shapes, distinguished by the grp= field:
             [tr] ts=<us> dur=<us> tid=<lane> name=<scope>                      (manual scope)
             [tr] ts=<us> dur=<us> tid=<lane> grp=<group> name=<frame>::<stage>  (a graph node)
Output : a JSON file that chrome://tracing and https://ui.perfetto.dev load directly.

TWO NAMES ARE CARRIED, AND NEITHER IS AN OS THREAD:

  * `tid` is a LANE -- a frame, hashed from its name. The thread pool hands a frame's nodes to
    whichever worker is free, so lanes have to be frames for the timeline to show a frame's shape.
  * `grp` is the COLLECTOR whose graph drove that node (empty at the top level). That is the
    install tree, so it becomes the process group: FRender's features end up inside a "FRender"
    group, sitting under the top-level group that holds FRender's own rows.

The left panel therefore reads as the architecture -- a collector and the frames it drives --
rather than as a list of thread ids.

ONE COSMETIC LIMIT: Perfetto prints each track's numeric id after its name ("FConfig 3"), and
Chrome JSON carries no field that turns that off. The ids are therefore renumbered to a SMALL
per-group index instead of the hash the engine logs, which is as close to "just the name" as this
format allows. Removing the id entirely needs Perfetto's native protobuf format, where a
TrackDescriptor sets the track name on its own.

Usage:
    maho_python.bat Tools/trace_to_chrome.py [input.txt] [output.json]

With no arguments it reads ./Profile_trace.txt (the working directory the engine runs in) and
writes the matching .json beside it.
"""

import json
import pathlib
import re
import sys

# A graph node carries grp=; a manual scope does not. Two patterns rather than one optional group,
# because the group may legitimately be EMPTY (top level) and "absent" must stay distinguishable
# from "empty" -- it is what tells a node event from a manual one.
NODE = re.compile(r"^\[tr\] ts=(\d+) dur=(\d+) tid=(\d+) grp=(\S*) name=(.+)$")
SCOPE = re.compile(r"^\[tr\] ts=(\d+) dur=(\d+) tid=(\d+) name=(.+)$")

# The top-level group (the host graph, which has no frame name of its own).
ROOT = 1


def convert(src: pathlib.Path, dst: pathlib.Path) -> tuple[int, int, int]:
    nodes = []      # (ts, dur, lane, group, frame, stage)
    scopes = []     # (ts, dur, lane, name)

    with src.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if (m := NODE.match(line.strip())) is not None:
                frame, _, stage = m.group(5).partition("::")
                nodes.append((int(m.group(1)), int(m.group(2)), int(m.group(3)),
                              m.group(4), frame, stage))
            elif (m := SCOPE.match(line.strip())) is not None:
                scopes.append((int(m.group(1)), int(m.group(2)), int(m.group(3)), m.group(4)))

    # A lane IS a frame (the lane id is a hash of the frame name), so the node events name their
    # own row. A group is likewise a property of the lane: every node on it was driven by the
    # collector that owns the graph it was submitted to.
    lane_frame: dict[int, str] = {}
    lane_group: dict[int, str] = {}
    for ts, _, lane, group, frame, _ in nodes:
        lane_frame[lane] = frame
        lane_group[lane] = group

    # A MANUAL scope carries no group of its own -- it ran inside whichever node was executing on
    # its thread, so the lane it landed on already says which frame (and therefore which group)
    # it belongs to.
    def group_of(lane: int) -> str:
        return lane_group.get(lane, "")

    # One row per (group, lane), not per lane: a frame type installed into TWO collectors gets two
    # instances, and the lane id is a hash of the frame NAME, so those two instances share a lane
    # and would otherwise be merged into one row.
    first_ts: dict[tuple[str, int], int] = {}
    for ts, _, lane, group, _, _ in nodes:
        key = (group, lane)
        first_ts[key] = min(first_ts.get(key, ts), ts)
    for ts, _, lane, _ in scopes:
        key = (group_of(lane), lane)
        first_ts[key] = min(first_ts.get(key, ts), ts)

    # One Chrome "process" per collector, so the panel can collapse a collector's features under
    # its name. The group name IS the collector's frame name, which is exactly the row those
    # features are sub-blocks OF.
    group_pid = {"": ROOT}
    for group in sorted({g for g, _ in first_ts if g}):
        group_pid[group] = len(group_pid) + 1

    # Row order: a collector's sub-frames top to bottom in the order the schedule runs them. A
    # frame's first event's timestamp IS that order -- the in-frame chain serializes a frame's
    # stages -- so no separate ordering data is needed.
    #
    # The row index becomes the tid, and Perfetto prints the tid after the track name ("FConfig 3").
    # Chrome JSON has no way to suppress that, so the index is kept SMALL (replacing nine-digit
    # lane hashes) rather than removed -- see the note in the module docstring.
    track: dict[tuple[str, int], tuple[int, int]] = {}
    for group in sorted(group_pid):
        lanes = sorted((lane for g, lane in first_ts if g == group),
                       key=lambda lane: (first_ts[(group, lane)], lane_frame.get(lane, "")))
        for index, lane in enumerate(lanes):
            track[(group, lane)] = (group_pid[group], index + 1)

    events = []

    def metadata(name: str, pid: int, tid: int | None, args: dict) -> None:
        entry = {"name": name, "ph": "M", "pid": pid, "args": args}
        if tid is not None:
            entry["tid"] = tid
        events.append(entry)

    metadata("process_name", ROOT, None, {"name": "Maho"})
    metadata("process_sort_index", ROOT, None, {"sort_index": 0})
    for group, pid in group_pid.items():
        if not group:
            continue
        metadata("process_name", pid, None, {"name": group})
        metadata("process_sort_index", pid, None, {"sort_index": pid})

    for (group, lane), (pid, tid) in track.items():
        metadata("thread_name", pid, tid, {"name": lane_frame.get(lane, str(lane))})
        metadata("thread_sort_index", pid, tid, {"sort_index": tid})

    # A manual scope's lane is the frame the scope ran inside (the lane is thread-local state that
    # the enclosing node set), so it lands on that frame's row with no extra bookkeeping.
    for ts, dur, lane, name in scopes:
        pid, tid = track.get((group_of(lane), lane), (ROOT, 1))
        events.append({
            "name": name, "cat": "cpu", "ph": "X", "ts": ts, "dur": dur,
            "pid": pid, "tid": tid,
        })
    for ts, dur, lane, group, frame, stage in nodes:
        pid, tid = track[(group, lane)]
        events.append({
            "name": stage, "cat": "graph", "ph": "X", "ts": ts, "dur": dur,
            "pid": pid, "tid": tid,
            "args": {"frame": frame, "group": group or "Maho"},
        })

    payload = {"traceEvents": events, "displayTimeUnit": "ms"}
    dst.write_text(json.dumps(payload), encoding="utf-8")
    return len(events), len(group_pid), len(track)


def main() -> int:
    src = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path("Profile_trace.txt")
    dst = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else src.with_suffix(".json")

    if not src.is_file():
        print(f"[trace] no such trace: {src}")
        return 1

    count, groups, lanes = convert(src, dst)
    if count == 0:
        print(f"[trace] {src} had no [tr] lines -- was MAHO_TRACE set for that run?")
        return 1

    print(f"[trace] {count} events, {lanes} frame lane(s), {groups} group(s)")
    print(f"[trace] {src} -> {dst}")
    print("[trace] open it at https://ui.perfetto.dev (or chrome://tracing)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
