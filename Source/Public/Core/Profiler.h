#pragma once

// Profiler -- CPU scope tracing for the profiling UI. CORE infrastructure: no dependencies, and
// it lives in Maho.dll so any layer can instrument itself with a single macro and no plugin
// load, no cross-module call, no registry.
//
// Form follows MAHO_TRACE_STAGES (Source/Private/Core/FrameGraph.cpp): the env switch is read
// ONCE, so with tracing off the whole thing is a predictable branch, and everything on the hot
// path is a static string -- no allocation, no formatting library, no lock of yours.
//
//   void FScene::EnsureTargets(FRender& R)
//   {
//       MAHO_TRACE_SCOPE("Render", "prepare this frame's targets");
//       ...
//   }
//
// With MAHO_TRACE set (any value), each scope writes ONE line when it closes:
//
//   [tr] ts=<us> dur=<us> lane=<Group> slot=<n> worker=<k> owner=<Frame> name=<Class>::<Function> tip=<text>
//   [tr] ts=<us> dur=<us> lane=<Group> slot=<n> worker=<k> owner=<Frame> grp=<Group> name=<Frame>::<Stage> tip=<text>
//   [tr] ts=<us> dur=<us> lane=Global  slot=<n> worker=<k> owner=<Frame> grp=<Group> name=<Frame>::<Stage> tip=<text>
//
// THE ROW AN EVENT IS DRAWN ON IS `lane` + `owner` + `slot`:
//   * `lane` (the GROUP: "Engine", "Render", "RHIServer") is the Perfetto PROCESS -- one collapsible
//     section per architectural partition, so the left panel reads as the architecture.
//   * `owner` + `slot` is the TRACK inside it. `owner` is the frame that was running (the same name
//     the node bars carry, and for a resident thread its own role name); `slot` is the in-flight ring
//     slot, which is what separates the SEVERAL INSTANCES of that frame that are in flight at once
//     (3 by default -- without it, instance N and instance N+1 partially overlap).
//
// Why that pairing and not the worker thread: a track can only NEST bars or SEQUENCE them, never
// overlap them (Perfetto moves any offender onto a spill track and reports an import error). One
// frame instance's stages form a TOTAL ORDER -- the bridge chains them -- so a (owner, slot) track
// nests by construction, and it reads as "what each feature was doing, per pipeline position". The
// worker is emitted too (`worker=`) but only as information: it is who ran the node, not what the
// row means.
//
// Because every graph node runs on the SAME FThreadPool, every TASK bar is ALSO mirrored onto the
// reserved `Global` group: that lane is the one place where "the engine's whole parallel schedule
// at this instant" is visible in a single row. The two events are identical (ts/dur/name/tip) and
// are filtered as a PAIR, so the file never shows a node on one lane and not the other.
//
// `grp=` is what tells a TASK bar from a manual scope (the reader's two shapes) and doubles as the
// fold-group key; by construction it always equals `lane`. `tip=` is optional.
//
// (ts, dur, lane, name) is one mechanical rewrite away from Chrome Trace Event Format's complete
// events, which is how Tools/trace_to_chrome.py turns this into a file that chrome://tracing and
// Perfetto load directly. `ts` is microseconds since the first traced call, on a MONOTONIC clock,
// so it is immune to wall-clock adjustments and comparable across threads.
//
// Two more environment switches, both read once:
//
//   MAHO_TRACE_MIN_US=<n>   duration floor for TASK bars (default 0 = keep all). Task bars are
//                           generated for EVERY task, so they are what makes a trace expensive;
//                           a manual scope is a human decision and is never filtered. Note the
//                           cost of filtering: rows are derived from events, so a frame whose
//                           every bar falls below the floor loses its row.
//   MAHO_TRACE_STAGES       (see Source/Private/Core/FrameGraph.cpp) enter/exit bracketing for
//                           the crash case, where the last stage ENTERED names the culprit.
//
// Note what this is NOT: a full event stream. Each scope costs two clock reads plus one line of
// buffer, so instrument the tens of scopes that matter, not every call in the frame.

#include <Core/Export.h>

#include <cstdint>
#include <functional>

namespace Maho
{

/** Recording is on. Reads MAHO_TRACE once; the answer never changes while the process lives. */
MAHO_API bool TraceEnabled();

/** Monotonic microseconds since the trace origin (the first traced call). The `ts` we emit. */
MAHO_API std::uint64_t TraceNowMicros();

/** Duration floor for automatically generated TASK bars (MAHO_TRACE_MIN_US, default 0 = all).
 *  Manual scopes are never filtered -- see the note at the top of this header. */
MAHO_API std::uint64_t TraceTaskFloorMicros();

/** The reserved group: it owns the mirrored node lane, and it is where anything without a group
 *  of its own lands. Always registered. */
MAHO_API const char* TraceGroupGlobal();

/** This thread's dense index -- the `worker=` field, and half of the row an event lands on (the
 *  other half is its group). Assigned once per thread under a lock, cached in thread-local storage,
 *  so the steady-state cost is a load; an OS tid is deliberately NOT used, because it is both wide
 *  and nondeterministic, and the number only has to be stable within one trace. */
MAHO_API std::uint32_t TraceThreadIndex();

/** Register a GROUP -- the architectural partition a lane stands for ("Engine", "Render",
 *  "RHIServer", ...). Idempotent, and the only way a name enters the trace's vocabulary: register
 *  the same name twice and the FIRST pointer comes back, so one group is one pointer and a lane can
 *  never split over two spellings of it. Call it where the group is introduced (a driver's init, a
 *  resident thread's startup) -- not per event. */
MAHO_API const char* RegisterTraceGroup(const char* Name);

/** The pointer to emit for `Name`: the registered one, or `Global` when the name is unknown (which
 *  also WARNS once per unknown name). A typo must not silently mint a lane -- that shows up as
 *  "the timeline looks wrong" long after the fact, which is the expensive kind of bug. `nullptr`
 *  and `""` mean "no group of my own" and resolve to `Global` without a warning. */
MAHO_API const char* ResolveTraceGroup(const char* Name);

/** Record one completed MANUAL scope (never filtered). Called by FScopedTrace's destructor.
 *  `Func` is optional: the enclosing function's own name, for scopes whose NAME is a hand-written
 *  section label instead (see MAHO_TRACE_SCOPE_SECTION) -- it rides along so a hover still says
 *  where the section lives. */
MAHO_API void TraceEmit(const char* Name, const char* Tip, const char* Func,
	std::uint64_t StartMicros, std::uint64_t DurMicros);

/** Record one TASK bar -- `Group :: First :: Second`, optionally mirrored onto `Global` (same
 *  ts/dur/name/tip), and filtered by MAHO_TRACE_MIN_US as a pair. Called by FScopedTracePair's
 *  destructor; callers rarely need it.
 *
 *  `Group` selects the lane (an empty string means `Global`), `First`/`Second` are the two halves
 *  the scheduler knows the node by -- one formatted line at write-out time is cheaper than
 *  concatenating at the call site, and keeps both halves static.
 *
 *  `Phase` is the ring slot the node ran in (negative = the caller has no phase, e.g. a resident
 *  thread's task): when no explicit `Tip` is given it becomes the tooltip `frame=<First> phase=<n>`,
 *  which is what makes a bar hoverable back to the frame and the in-flight slot it belonged to. */
MAHO_API void TraceEmitPair(const char* Group, const char* First, const char* Second, const char* Tip,
	std::int32_t Phase, bool bMirrorToGlobal, std::uint64_t StartMicros, std::uint64_t DurMicros);

/** Record one SYNCHRONIZATION POINT: `Gate` reached its end, and that is what released `Next`.
 *
 *  It is the one dependency a scheduler actually ENFORCED -- a node may wait for several predecessors,
 *  but only the last one to finish unblocks it -- so this is ONE record per dispatched node, not one
 *  per declared edge (which would be hundreds of lines per frame and no more information). A viewer
 *  draws it as a FLOW arrow from the gate's row to the released node's row, which is what turns "these
 *  two bars are far apart" into "this one waited for that one".
 *
 *  Both halves are the `{group, owner, slot}` triple that decides the ROW (see the top of this header),
 *  plus the gate's END timestamp: the released node starts at the moment of this call. All strings must
 *  be static storage. Emitted lines look like:
 *
 *      [tr] flow a_ts=<us> a_lane=<g> a_owner=<o> a_slot=<n> b_ts=<us> b_lane=<g> b_owner=<o> b_slot=<n> */
MAHO_API void TraceEmitFlow(const char* GateGroup, const char* GateOwner, const char* GateStage,
	std::int32_t GateSlot, std::uint64_t GateEndMicros,
	const char* NextGroup, const char* NextOwner, const char* NextStage, std::int32_t NextSlot);

/** Flush every thread's pending events to the trace file. Called at exit; callable by hand so a
 *  host can snapshot mid-run (each call appends whatever is buffered). */
MAHO_API void TraceFlush();

/** The identity a submitted TASK carries into the trace: which GROUP it belongs to, and what it is
 *  doing there.
 *
 *  It exists so the trace can be opened at the ONE place every piece of work passes through --
 *  the pool worker that runs it (FThreadPool) or the resident thread that runs it (FThreadedServer)
 *  -- rather than at each submitter's call site. The submitter is the only party that KNOWS the
 *  identity, so it hands it over together with the task:
 *
 *      Pool.Submit(Lane, {"Render", Stage.name(), "Scene"}, [this] { ... });
 *
 *  All three strings must be static storage (a literal, a registered group, a stage's
 *  type_info::name()): the event outlives the task and keeps the pointers.
 *
 *  An empty Name means "not traced": opening a scope with no label would only put unnameable bars
 *  on the default lane. */
struct FTaskTrace
{
	const char* Group = "";   // the group (lane) this task belongs to; "" = Global
	const char* Name  = "";   // the frame/role that runs it
	const char* Stage = "";   // what it is doing (a stage type's name, a phase label, ...)
	std::int32_t Phase = -1;  // the ring slot it ran in; -1 = no phase to report
};

/** Wrap a task body so that ITS bar is opened where the task RUNS -- without the executor having to
 *  know anything about tracing.
 *
 *  This is what keeps the pool and the resident workers type-agnostic: they enqueue and run ordinary
 *  closures, while the party that KNOWS the identity (the submitter -- the frame graph, the role that
 *  owns a server) wraps the body on its way in. The bar still opens at the task boundary, on whichever
 *  thread runs it (a pool worker, or the thread a Flush helped drain from), so nothing the old
 *  hard-wired hook bought is lost.
 *
 *  This form MIRRORS onto Global (a graph node does). With tracing off -- or when the identity has no
 *  Name -- the body is returned UNCHANGED: no wrapper, no allocation, nothing. */
MAHO_API std::function<void()> TraceWrap(std::function<void()> Body, const FTaskTrace& Identity);

/** The same, for a RESIDENT WORKER's task: its own lane (the role name), and no Global mirror --
 *  a resident thread is not part of the graph's parallel schedule. Registration of the role's group
 *  happens here too (idempotent), which is why a server never has to mention the trace at all. */
MAHO_API std::function<void()> TraceWrapResident(std::function<void()> Body, const char* Role,
	const char* Stage);

/** RAII scope for a MANUAL point. Takes the start timestamp on construction and records on
 *  destruction -- so an early return, an exception, or a break all still produce a complete event.
 *
 *  `InGroup` may be null, which means "keep the lane this thread is already on" -- what a scope
 *  inside a frame's stage wants, since the stage already established it. Passing a group makes this
 *  scope ESTABLISH that lane for its duration (nested scopes and any work it starts land there) and
 *  restores the previous one on exit.
 *
 *  With tracing off this is a pointer copy and a branch: the timestamp stays 0 and the destructor
 *  does nothing. */
class MAHO_API FScopedTrace
{
public:
	/** `InName` is what the bar shows, `InFunc` is where it came from (optional, for a bar whose
	 *  name is a hand-written section label rather than the function's own -- see
	 *  MAHO_TRACE_SCOPE_SECTION). */
	explicit FScopedTrace(const char* InGroup, const char* InTip, const char* InName,
		const char* InFunc = nullptr);
	~FScopedTrace();

	FScopedTrace(const FScopedTrace&) = delete;
	FScopedTrace& operator=(const FScopedTrace&) = delete;
	FScopedTrace(FScopedTrace&&) = delete;
	FScopedTrace& operator=(FScopedTrace&&) = delete;

private:
	const char*   Name = nullptr;
	const char*   Tip = nullptr;
	const char*   Func = nullptr;
	const char*   ResolvedGroup = nullptr;   // null when the lane is inherited
	const char*   PreviousGroup = nullptr;
	std::uint64_t Start = 0;
};

/** FScopedTrace for a two-part name -- and the thing that ESTABLISHES a lane.
 *
 *  Everything recorded while this scope is alive is attributed to its GROUP: nested scopes, plain
 *  MAHO_TRACE_SCOPE points, and work that runs on OTHER threads. The lane is the group (not the
 *  frame) because the two questions a reader asks are "which partition is busy" and "what is the
 *  engine running in parallel" -- both answered by grouping, neither by a frame.
 *
 *  `First`/`Second` must be static storage, exactly like the single-name form. The definitions are
 *  out of line because the lane-local state owned by the .cpp. */
class MAHO_API FScopedTracePair
{
public:
	FScopedTracePair(const char* InGroup, const char* InFirst, const char* InSecond, const char* InTip,
		std::int32_t InPhase, bool bMirrorToGlobal);
	~FScopedTracePair();

	FScopedTracePair(const FScopedTracePair&) = delete;
	FScopedTracePair& operator=(const FScopedTracePair&) = delete;
	FScopedTracePair(FScopedTracePair&&) = delete;
	FScopedTracePair& operator=(FScopedTracePair&&) = delete;

private:
	const char*   Group = nullptr;
	const char*   First = nullptr;
	const char*   Second = nullptr;
	const char*   Tip = nullptr;
	std::int32_t  Phase = -1;
	bool          bMirror = false;
	const char*   PreviousGroup = nullptr;
	std::int32_t  PreviousSlot = -1;
	const char*   PreviousFrame = nullptr;
	std::uint64_t Start = 0;
};

} // namespace Maho

/** Unique name for the scope object (a plain counter, so it works at any scope). */
#define MAHO_TRACE_CONCAT_(A, B) A##B
#define MAHO_TRACE_CONCAT(A, B) MAHO_TRACE_CONCAT_(A, B)

/**
 * Trace one MANUAL scope IN A GROUP, with a tooltip. The name is the function's own: `__FUNCTION__`
 * gives `Class::Function` on MSVC and is static storage, which is exactly what an event needs -- so
 * the class and the function are never retyped (and never disagree with the code).
 *
 *   MAHO_TRACE_SCOPE("Render", "prepare this frame's targets");
 *
 * `Group` may be `nullptr`, which means "keep the lane the enclosing frame or task already
 * established" -- what a scope inside a driven stage wants, and what most call sites pass. Pass a
 * registered group name instead when the scope runs with no frame context of its own.
 */
#define MAHO_TRACE_SCOPE(Group, Tip) \
	::Maho::FScopedTrace MAHO_TRACE_CONCAT(_MahoScope, __LINE__)(Group, Tip, __FUNCTION__)

/**
 * A manual scope whose bar is named by a HAND-WRITTEN SECTION instead of the enclosing function.
 *
 * `__FUNCTION__` is the right name for a scope that IS the function's body, but it makes every scope
 * inside one function carry that same name -- three instruments in `FUIFeature::InitViews` all read
 * "FUIFeature::InitViews", which is unreadable on a timeline (they are told apart only by their
 * tooltip). This form names each one for what it does; the function still rides along in `func=`, so a
 * hover keeps saying where the section lives.
 *
 *   MAHO_TRACE_SCOPE_SECTION("Render", "translate views", "把注册的视图翻译成 ImGui 命令");
 */
#define MAHO_TRACE_SCOPE_SECTION(Group, Section, Tip) \
	::Maho::FScopedTrace MAHO_TRACE_CONCAT(_MahoSection, __LINE__)(Group, Tip, Section, __FUNCTION__)

/**
 * The TASK form: `Group :: First :: Second` on the group's lane, MIRRORED onto Global because every
 * graph node shares one FThreadPool. The frame graph hands its identity to the pool; the pool is
 * what calls this (FThreadPool::RunTracedTask).
 */
#define MAHO_TRACE_SCOPE3(Group, First, Second, Tip) \
	::Maho::FScopedTracePair MAHO_TRACE_CONCAT(_MahoScope3, __LINE__)(Group, First, Second, Tip, -1, true)

/**
 * A scope that lives on its own lane with NO owning collector -- a RESIDENT WORKER (a
 * FThreadedServer role) rather than a frame some graph drives. `Group` is the thread's own name
 * (`GetThreadName()`), so the lane and the thread are one concept, and there is no Global mirror:
 * a resident thread is not part of the graph's parallel schedule.
 *
 *   MAHO_TRACE_SCOPE_LANE(GetThreadName(), "Marshal", "submitted batch");
 */
#define MAHO_TRACE_SCOPE_LANE(Group, Second, Tip) \
	::Maho::FScopedTracePair MAHO_TRACE_CONCAT(_MahoLane, __LINE__)(Group, Group, Second, Tip, -1, false)
