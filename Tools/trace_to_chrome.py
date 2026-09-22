#!/usr/bin/env python3
"""Convert a Maho CPU trace into Chrome Trace Event Format.

NOTE -- this is the FALLBACK path, not the normal one. The engine now writes Perfetto's own protobuf
itself (Plugins/Common/Log/Private/Trace.cpp: FProtoWriter, track_descriptor / track_event packets,
flows as flow_ids/terminating_flow_ids), so the ordinary answer to "I want a timeline" is to open
Profile_trace.perfetto_trace directly -- no converter, no timestamp-to-slice guessing, and the arrow
ownership is written into the file instead of being re-derived here. Keep this script for the two
cases the native path does not cover: reading an OLD text trace (something already captured before the
native writer existed), and wanting the JSON itself for a tool that only eats JSON.

Input  : Profile_trace.txt -- the "[tr] ..." lines the engine writes when MAHO_TRACE is set (see
         Plugins/Common/Log/Public/Trace.h). Select it with MAHO_TRACE_FORMAT=0 (text only) or 2 (both).

             [tr] ts=<us> dur=<us> lane=<Group> worker=<k> owner=<Track> [stage=<Short>] [func=<Fn>] name=<Label> [tip=<text>]
             [tr] flow ts=<us> lane=<Group> owner=<Track> stage=<Short> from=<Target> fromStage=<Short> off=<0|-1>

Output : a JSON file that chrome://tracing and https://ui.perfetto.dev load directly.

HOW THE ROWS ARE BUILT:

  * `lane` (the GROUP: "Engine", "Render", "RHIServer") becomes the Perfetto PROCESS: one collapsible
    section per architectural partition, so the left panel reads as the architecture.
  * `owner` (the TRACK) becomes the TRACK inside it: a frame's static name for a stage bar (FScene,
    FDrawTriangleFeature, FUIFeature, ...), the lane itself for a resident thread or a free scope.
    A partition's rows are then the things that make it up.

  That pairing is what a timeline can actually draw. A track may only NEST bars or SEQUENCE them --
  never overlap them -- and any offence is reported as an import error and moved onto a spill track.
  Per FRAME is safe because the engine chains a frame's stages in order (and each stage against its
  own previous frame: the next frame's `IBeginRender` waits for this frame's), so the bars of one
  (group, owner) nest by construction. Per GROUP alone is NOT safe: two independent features of one
  frame really do run at the same time, and no row can show that.

  `worker` (which thread ran it) travels in args: a stage is dispatched to whichever pool worker is
  free, so the thread answers "who ran it" on hover without deciding layout.

Event names keep the engine's own wording: a stage bar keeps the label the engine wrote for it (a
hand-written stage label, or a legacy `<Frame>::<Stage>`), a section/scope the label or function it
was opened with.

Usage:
    maho_python.bat Tools/trace_to_chrome.py [input.txt] [output.json]

With no arguments it reads ./Profile_trace.txt (the working directory the engine runs in) and
writes the matching .json beside it.
"""

import json
import pathlib
import re
import sys

# -- the current format -------------------------------------------------------------------------
# ONE regex for every form of bar: a stage BODY writes `stage=` and no `func=`; a SECTION inside a
# stage writes both (it INHERITS the ambient stage, which is what puts it on the stage's row) and is
# named by a hand-written label; a SCOPE writes neither and is named by its function. Every field
# after `lane` is optional, so a trace from before the group rework still matches: there `grp=` is
# the node's ORIGIN group (what the older Global mirror kept) and `slot=` the in-flight ring slot.
BAR = re.compile(
    r"^\[tr\] ts=(\d+) dur=(\d+) lane=(\S+)(?: slot=(\d+))?(?: worker=(\d+))?(?: owner=(\S+))?"
    r"(?: grp=(\S*))?(?: stage=(\S+))?(?: func=(\S+))? name=(.+?)(?: tip=(.*))?$")

# -- a declared dependency edge, written by the stage bar that waited for the other one ----------
# `from` is the target frame's name and `off` the declared frame offset (0 = this -1 = the
# previous one); both describe an edge whose ends only exist as BARS, so the reader binds them.
FLOW = re.compile(
    r"^\[tr\] flow ts=(\d+) lane=(\S+) owner=(\S+) stage=(\S+) from=(\S+) fromStage=(\S+)"
    r" off=(-?\d+)$")

# -- files from before the group rework: a numeric lane id, and the frame name in the event -------
OLD_NODE = re.compile(r"^\[tr\] ts=(\d+) dur=(\d+) tid=(\d+) grp=(\S*) name=(.+?)(?: tip=(.*))?$")
OLD_SCOPE = re.compile(r"^\[tr\] ts=(\d+) dur=(\d+) tid=(\d+) name=(.+?)(?: tip=(.*))?$")

# The fold section for events that belong to no group of their own.
ROOT_NAME = "Maho"
ROOT = 1


def convert(src: pathlib.Path, dst: pathlib.Path, drops: set | None = None,
            spacer: bool = True, with_flows: bool = True) -> tuple[int, int, int, int, int]:
    # Every event, normalized to:
    #   (ts, dur, group, owner, worker, is_node, name, tip, origin, stage, func)
    records = []
    flows = []      # (ts, lane, owner, stage, from_name, from_stage)
    # Old files only: which frame a numeric lane belonged to (derived from the node events).
    old_lane_frame = {}

    with src.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            text = line.strip()
            if (m := FLOW.match(text)) is not None:
                # `off=` is KEPT: it is what separates the two kinds of edge. `off=0` is a stage ordered
                # against another stage of the SAME frame (the in-frame chain, or a declared edge inside
                # one frame); `off=-1` is the cross-frame pipeline edge ("this frame's first stage after
                # the last frame's last one") -- the only ordering the scheduler does not give for free,
                # and the only arrow worth drawing on a timeline. See the drawing loop for the filter.
                flows.append((int(m.group(1)), m.group(2), m.group(3), m.group(4), m.group(5),
                              m.group(6), int(m.group(7))))
                continue
            if (m := BAR.match(text)) is not None:
                lane, owner = m.group(3), m.group(6) or ""
                stage, func = m.group(8) or "", m.group(9) or ""
                # A stage BODY is the one form that writes `stage=` and no `func=`. A section inside a
                # stage inherits that stage's name, but it is named by a hand-written label, so `func=`
                # is what tells the two apart. `grp=` marks a pre-rework node bar, which carries no
                # stage of its own.
                is_node = (m.group(7) is not None) or (bool(stage) and not func)
                records.append((int(m.group(1)), int(m.group(2)), lane, owner,
                                int(m.group(5) or -1), is_node, m.group(10), m.group(11) or "",
                                m.group(7) or lane, stage, func))
            elif (m := OLD_NODE.match(text)) is not None:
                frame, _, stage = m.group(5).partition("::")
                old_lane_frame[m.group(3)] = frame
                records.append((int(m.group(1)), int(m.group(2)), m.group(4) or ROOT_NAME, frame,
                                -1, True, m.group(5), m.group(6) or "", m.group(4) or ROOT_NAME,
                                stage, ""))
            elif (m := OLD_SCOPE.match(text)) is not None:
                records.append((int(m.group(1)), int(m.group(2)), "", m.group(4), -1, False,
                                m.group(4), m.group(5) or "", "", "", ""))
            else:
                continue

    # A manual scope in an old file: give it the frame of the lane it landed on, so it stays on the
    # same row it used to.
    resolved = []
    for ts, dur, group, owner, worker, is_node, name, tip, origin, stage, func in records:
        if not is_node and not group:
            owner = old_lane_frame.get(owner, owner)
            group = ROOT_NAME
            origin = ROOT_NAME
        resolved.append((ts, dur, group, owner, worker, is_node, name, tip, origin, stage, func))
    records = resolved

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

    for ts, dur, group, owner, worker, is_node, name, tip, origin, stage, func in records:
        pid, tid = track[(group, owner or group)]
        if is_node:
            # A node bar keeps its full name; the stage key, the row it ran on and the partition it
            # belongs to go to args, where a hover shows them.
            args = {"group": origin}
            if stage:
                args["stage"] = stage
            if tip:
                args["tip"] = tip
            if owner:
                args["owner"] = owner
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
            if stage:
                # The stage it was opened inside: a section belongs to a stage's row but is not a node.
                args["stage"] = stage
            if tip:
                args["tip"] = tip
            if owner:
                args["owner"] = owner
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

    # A one-microsecond bar contains no integer instant, so a flow event can never be placed strictly
    # inside it: the viewer would report no containing slice and pile the arrow onto whichever slice
    # happens to have id 0. Widening is resolved per endpoint below (after the spacer pass, so it can
    # see whether there is room) -- NOT here, where it would re-create what the spacer removes.

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

    # -- declared dependencies as FLOW arrows -------------------------------------------------------
    # Both ends are BOUND TO ACTUAL BARS, because a viewer draws a flow only between slices it can
    # attach to, and a timestamp alone is not enough: the hint carries the two ends as (lane, owner,
    # stage), and the bar it names may be several frames away. So the arrow is pinned to the START of
    # each of the two STAGE bars -- the one instant a bar is certainly the innermost slice there (the
    # tie-break pass above keeps children off their parent's first microsecond).
    #
    # Only a stage body is a candidate: a section inherits its stage's name (that is how it lands on
    # the row), so binding by stage alone would happily hook the arrow onto a section inside the bar.
    graph_bars: dict[tuple[int, int], list] = {
        row: [bar for bar in bars if bar["cat"] == "graph"] for row, bars in per_track.items()}

    def BindBar(row, stage, ts, b_before):
        bars = graph_bars.get(row)
        if not bars or not stage:
            return None
        best = None
        for bar in bars:
            if bar["args"].get("stage") != stage:
                continue
            if (bar["ts"] <= ts) if b_before else (bar["ts"] >= ts):
                if best is None or abs(bar["ts"] - ts) < abs(best["ts"] - ts):
                    best = bar
        return best

    drawn_flows = 0
    skipped_flows = 0
    one_shot_flows = 0
    chain_flows = 0
    unanchored_flows = 0

    # A flow event must sit STRICTLY inside its bar (`start < ts < end`): that is how a viewer decides
    # which slice the arrow belongs to, and a ts equal to the bar's start is reported as "no slice"
    # (slice_out = 0) -- every unattached arrow then piles onto whichever slice carries id 0. So each
    # endpoint is placed at +1us, which needs the bar to be >= 2us wide; a bar the spacer pass trimmed
    # to 1us is widened ONLY when the next bar on its row leaves the room, and the arrow is dropped
    # when it does not -- an arrow that cannot be attached is worse than a missing arrow.
    row_starts: dict[tuple, list[int]] = {
        row: sorted(bar["ts"] for bar in bars) for row, bars in per_track.items()}

    def AnchorIn(bar, row):
        if bar["dur"] >= 2:
            return bar["ts"] + 1
        for start in row_starts.get(row, ()):
            if start > bar["ts"]:
                if start - bar["ts"] >= 3:
                    bar["dur"] = 2
                    return bar["ts"] + 1
                return None
        bar["dur"] = 2
        return bar["ts"] + 1
    # ONE-SHOT stages (init / install / teardown) run once, so a loop stage that declares a dependency
    # on one is ordered by it only in the frame the install happened -- after that the edge is a no-op
    # the graph keeps re-creating, and its arrow would span the whole session (an install hint at ts~0
    # bound to a frame 1.2s later reads as a floating arrowhead with its line off-screen).
    #
    # Two rules, both needed: a stage that never repeats on its row is one-shot, and -- the sharper
    # one -- an arrow longer than a few of its row's OWN frames is not a frame-local edge at all. The
    # row's period is the median gap between its bars, so this needs no threshold tuned per stage.
    stage_repeats: dict[tuple, int] = {}
    for row, row_bars in graph_bars.items():
        for bar in row_bars:
            key = (row, bar["args"].get("stage"))
            stage_repeats[key] = stage_repeats.get(key, 0) + 1

    row_period: dict[tuple, int] = {}
    for row, row_bars in graph_bars.items():
        starts = sorted(bar["ts"] for bar in row_bars)
        if len(starts) < 8:
            continue
        # The row's frame period is the gap that RECURS: inside one frame the gaps are irregular (the
        # stages differ in length) while the gap between frames is steady, so the modal 100us bucket
        # is the period. A median would average the two and end up cutting real frame-local arrows.
        buckets: dict[int, int] = {}
        for a, b in zip(starts, starts[1:]):
            if b - a >= 100:
                key = (b - a) // 100
                buckets[key] = buckets.get(key, 0) + 1
        if buckets:
            best = max(buckets.items(), key=lambda kv: kv[1])[0]
            row_period[row] = (best + 1) * 100

    for index, flow in enumerate(flows if with_flows else []):
        ts, lane, owner, stage, from_name, from_stage, off = flow
        # ONLY THE CROSS-FRAME PIPELINE EDGE IS DRAWN. Everything else -- the in-frame chain and the
        # same-frame declared edges -- is either already readable from the bar order or paints a whole
        # row solid at three arrows per frame, which is what turned this timeline into a brown mass.
        # What is left is the one ordering the scheduler does not give for free: `off=-1`, the edge that
        # makes frame N+1 wait for frame N, i.e. the pipeline itself.
        if off == 0:
            chain_flows += 1
            continue
        emitter_row = track.get((lane, owner))
        # The input lives on the TARGET frame's row, in this stage's own lane: a declared edge never
        # crosses a partition, and `from=` is the target frame's name, not a lane.
        target_row = track.get((lane, from_name))
        if (stage_repeats.get((emitter_row, stage), 0) < 2
                or stage_repeats.get((target_row, from_stage), 0) < 2):
            one_shot_flows += 1
            continue
        emitter = BindBar(emitter_row, stage, ts, False) if emitter_row else None
        producer = BindBar(target_row, from_stage, ts, True) if target_row else None
        if emitter is None or producer is None:
            # An endpoint's bar is gone -- its group was dropped, or MAHO_TRACE_MIN_US filtered the
            # stage out (a dropped bar takes its flow hints with it, so this is the honest count).
            skipped_flows += 1
            continue
        period = row_period.get(emitter_row)
        span_us = abs(emitter["ts"] - producer["ts"])
        # A frame-local arrow is at most a few frames long; a one-shot stage's arrow is hundreds (and
        # during the startup pause a real frame can be long). The 100ms floor is what covers the rows
        # that have NO period at all -- an engine-core row holds one or two install bars, so a period
        # cannot be computed there and the span rule must not be skipped: those rows are exactly where
        # the session-spanning arrows come from (their producer sits at ts~0).
        limit = max(4 * period, 100_000) if period else 100_000
        if span_us > limit:
            one_shot_flows += 1
            continue
        # IN-FRAME CHAIN arrows are dropped: producer and consumer are stages of the SAME frame on the
        # SAME row, and the bars are already laid out in exactly that order -- an arrow adds nothing but
        # ink, and three of them per frame paint the row solid. What survives on a row is the CROSS-FRAME
        # arrow (off=-1: "this frame's first stage after the last frame's last one"), whose span is one
        # frame: that one IS the pipeline, and it is one arrow per frame, not a texture.
        if period is not None and emitter_row == target_row:
            span = abs(emitter["ts"] - producer["ts"])
            if not (period // 2 <= span <= 2 * period):
                chain_flows += 1
                continue
        # `s` on the producer (the input the stage waited for), `f` on the stage that waited: the
        # arrow then reads input -> stage, which is the direction the dependency was declared.
        #
        # `cat` MUST match the category of the slices on both ends (they are all stage bars, i.e.
        # "graph"): a flow event whose category names a different track than the slices it points at
        # cannot be attached to them, and the viewer then falls back to pairing by timestamp across the
        # whole timeline -- which is how a frame-local edge turns into a line to some unrelated slice.
        # Event form: `ph: "s"` on the producer, `ph: "f"` on the consumer, one `id` per flow. NO `cat`:
        # a category makes the viewer look for a category track, and a flow whose category does not match
        # the slices' is parsed but never attached -- `slice_out` stays 0 and the arrow has no ends. The
        # two tss are inside their bars (see AnchorIn) so the lookup has something to find.
        drawn_flows += 1
        flow_id = index + 1
        s_ts = AnchorIn(producer, target_row)
        f_ts = AnchorIn(emitter, emitter_row)
        if s_ts is None or f_ts is None:
            unanchored_flows += 1
            continue
        events.append({"name": f"{from_name}::{from_stage} -> {stage}" if from_name and from_stage
                       else "sync", "ph": "s", "id": flow_id,
                       "ts": s_ts, "pid": target_row[0], "tid": target_row[1]})
        events.append({"name": "sync", "ph": "f", "id": flow_id,
                       "ts": f_ts, "pid": emitter_row[0], "tid": emitter_row[1]})

    if one_shot_flows:
        print(f"[trace]   {one_shot_flows} flow hint(s) skipped: a one-shot (init/install/teardown) "
              f"stage at one end, so the arrow would span the session instead of a frame")
    if chain_flows:
        print(f"[trace]   {chain_flows} in-frame chain hint(s) skipped: same row, a fraction of a "
              f"frame apart -- the bars already read in that order, the arrows only painted it solid")

    # A SELF-IDENTIFYING marker, on its own row at the very top. Two conversions of the same raw trace
    # have IDENTICAL bars -- only the flows differ -- so nothing in the timeline tells a viewer which
    # one it is showing, and a viewer that restored a cached copy keeps showing the older arrows. This
    # slice names this conversion by file + counters: if it is not on screen, the trace on screen is
    # not this file.
    # A SELF-IDENTIFYING marker, at the very END of the timeline and on a row of its own. Two
    # conversions of the same raw trace have IDENTICAL bars -- only the flows differ -- so nothing in
    # the timeline tells a viewer which one it is showing. Its ts must NOT be 0: a viewer treats
    # slice id 0 as "no slice", and any flow event it cannot attach to a bar is reported against
    # slice 0 -- which is how a marker at ts~0 "collected" hundreds of unrelated arrows.
    rev_tid = 999
    rev_ts = max((e["ts"] for e in events if e.get("ph") == "X"), default=0) + 1000
    events.append({"name": "thread_name", "ph": "M", "pid": ROOT, "tid": rev_tid,
                   "args": {"name": "TRACE REV"}})
    events.append({"name": "thread_sort_index", "ph": "M", "pid": ROOT, "tid": rev_tid,
                   "args": {"sort_index": 9999}})
    events.append({
        "name": f"TRACE-REV {dst.name} flows={drawn_flows} one-shot-dropped={one_shot_flows}",
        "cat": "meta", "ph": "X", "ts": rev_ts, "dur": 1, "pid": ROOT, "tid": rev_tid,
        "args": {"file": dst.name, "flows": drawn_flows, "one_shot_dropped": one_shot_flows}})

    payload = {"traceEvents": events, "displayTimeUnit": "ms"}
    dst.write_text(json.dumps(payload), encoding="utf-8")
    return len(events), len(group_pid), len(track), drawn_flows, skipped_flows


def main() -> int:
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

    count, groups, lanes, flows_drawn, flows_skipped = convert(src, dst, drops, spacer, with_flows)
    if count == 0:
        print(f"[trace] {src} had no [tr] lines -- was MAHO_TRACE set for that run?")
        return 1

    skipped = "" if spacer else ", raw timing (no spacer)"
    if drops:
        skipped += f", dropped {','.join(sorted(drops))}"
    print(f"[trace] {count} events ({flows_drawn} flow arrows, {flows_skipped} skipped), "
          f"{lanes} track(s), {groups} group(s){skipped} -> {dst}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
