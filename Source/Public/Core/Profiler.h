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
//       MAHO_TRACE_SCOPE("FScene::EnsureTargets");
//       ...
//   }
//
// With MAHO_TRACE set (any value), each scope writes ONE line when it closes:
//
//   [tr] ts=<us> dur=<us> tid=<n> name=<scope>
//
// (ts, dur, tid, name) is exactly Chrome Trace Event Format's complete-event tuple, so turning
// this into a file that chrome://tracing and Perfetto load directly is a mechanical rewrite --
// no per-event parsing decisions to make. `ts` is microseconds since the first traced call, on
// a MONOTONIC clock, so it is immune to wall-clock adjustments and comparable across threads.
//
// Note what this is NOT: a full event stream. Each scope costs two clock reads plus one line of
// buffer, so instrument the tens of scopes that matter, not every call in the frame.

#include <Core/Export.h>

#include <cstdint>

namespace Maho
{

/** Recording is on. Reads MAHO_TRACE once; the answer never changes while the process lives. */
MAHO_API bool TraceEnabled();

/** Monotonic microseconds since the trace origin (the first traced call). The `ts` we emit. */
MAHO_API std::uint64_t TraceNowMicros();

/** Record one completed scope. Called by FScopedTrace's destructor; callers rarely need it. */
MAHO_API void TraceEmit(const char* Name, std::uint64_t StartMicros, std::uint64_t DurMicros);

/** Same, for a name that is naturally a PAIR (`frame :: stage`). The scheduler knows a node as a
 *  {name, stage, phase} triple, and neither half should have to be concatenated at the call site
 *  -- one formatted line at write-out time is cheaper and keeps both halves static.
 *
 *  `Group` is the COLLECTOR that drives this frame (empty at the top level): FRender's features
 *  are driven by FRender's own graph, so their events name "FRender" as the group and a viewer
 *  can lay them out as FRender's sub-blocks instead of as unrelated rows. */
MAHO_API void TraceEmitPair(const char* Group, const char* First, const char* Second,
	std::uint64_t StartMicros, std::uint64_t DurMicros);

/** Flush every thread's pending events to the trace file. Called at exit; callable by hand so a
 *  host can snapshot mid-run (each call appends whatever is buffered). */
MAHO_API void TraceFlush();

/** The identity a submitted TASK carries into the trace: who it belongs to, what it is doing.
 *
 *  It exists so the trace can be opened at the ONE place every piece of work passes through --
 *  the pool worker that runs it (FThreadPool) or the resident thread that runs it
 *  (FThreadedServer) -- rather than at each submitter's call site. The submitter is the only
 *  party that KNOWS the identity, so it hands it over together with the task:
 *
 *      Pool.Submit(Lane, {"FRender", Stage.name(), "Scene"}, [this] { ... });
 *
 *  All three strings must be static storage (a literal, a frame's GetName(), a stage's
 *  type_info::name()): the event outlives the task and keeps the pointers.
 *
 *  Name doubles as the LANE key -- it is what FScopedTracePair hashes to pick a row -- so a task
 *  lands on the row its submitter established, by construction.
 *
 *  An empty Name means "not traced": opening a scope with no label would only put unnameable
 *  bars on the default lane. */
struct FTaskTrace
{
	const char* Group = "";   // the collector DRIVING this task ("" = top level)
	const char* Name  = "";   // the frame/role that runs it; also the lane it is drawn on
	const char* Stage = "";   // what it is doing (a stage type's name, a phase label, ...)
};

/** RAII scope. Takes the start timestamp on construction and records on destruction -- so an
 *  early return, an exception, or a break all still produce a complete event.
 *
 *  With tracing off this is a pointer copy and a branch: the timestamp stays 0 and the
 *  destructor does nothing. Either way it needs no lane bookkeeping: it is constructed and
 *  destroyed inside one node's execution on one thread, so it simply records into whatever lane
 *  FScopedTracePair already established there. */
class MAHO_API FScopedTrace
{
public:
	explicit FScopedTrace(const char* InName) : Name(InName)
	{
		if (TraceEnabled())
		{
			Start = TraceNowMicros();
		}
	}

	~FScopedTrace()
	{
		if (Start != 0)
		{
			TraceEmit(Name, Start, TraceNowMicros() - Start);
		}
	}

	FScopedTrace(const FScopedTrace&) = delete;
	FScopedTrace& operator=(const FScopedTrace&) = delete;
	FScopedTrace(FScopedTrace&&) = delete;
	FScopedTrace& operator=(FScopedTrace&&) = delete;

private:
	const char*   Name;
	std::uint64_t Start = 0;
};

/** FScopedTrace for a two-part name -- and the thing that ESTABLISHES a lane.
 *
 *  Everything recorded while this scope is alive is attributed to `First`: nested scopes, plain
 *  MAHO_TRACE_SCOPE points, and work that runs on OTHER threads. That is what puts one frame's
 *  stages on one row of the timeline: the thread pool hands a frame's nodes to whichever worker
 *  is free, so a per-thread grouping shreds a frame across lanes and hides its real shape.
 *
 *  `Group` is whichever collector's graph is driving this frame (see TraceEmitPair); both halves
 *  must be static storage, exactly like the single-name form. The definitions are out of line
 *  because the lane stack is thread-local state owned by the .cpp. */
class MAHO_API FScopedTracePair
{
public:
	FScopedTracePair(const char* InGroup, const char* InFirst, const char* InSecond);
	~FScopedTracePair();

	FScopedTracePair(const FScopedTracePair&) = delete;
	FScopedTracePair& operator=(const FScopedTracePair&) = delete;
	FScopedTracePair(FScopedTracePair&&) = delete;
	FScopedTracePair& operator=(FScopedTracePair&&) = delete;

private:
	const char*   Group;
	const char*   First;
	const char*   Second;
	std::uint32_t PreviousLane = 0;
	std::uint64_t Start = 0;
};

} // namespace Maho

/** Unique name for the scope object (a plain counter, so it works at any scope). */
#define MAHO_TRACE_CONCAT_(A, B) A##B
#define MAHO_TRACE_CONCAT(A, B) MAHO_TRACE_CONCAT_(A, B)

/**
 * Trace one scope. `Name` must be a string LITERAL (or other static storage): the event keeps
 * the pointer and the name is written when the scope closes, long after the frame returns.
 *
 *   MAHO_TRACE_SCOPE("FRender::IRender");
 */
#define MAHO_TRACE_SCOPE(Name) ::Maho::FScopedTrace MAHO_TRACE_CONCAT(_MahoScope, __LINE__)(Name)

/** Trace a scope whose name is a pair (group :: frame :: stage), all three string literals. */
#define MAHO_TRACE_SCOPE3(Group, First, Second) \
	::Maho::FScopedTracePair MAHO_TRACE_CONCAT(_MahoScope3, __LINE__)(Group, First, Second)

/**
 * A scope that establishes its own lane with NO owning collector -- a RESIDENT WORKER (a
 * FThreadedServer role) rather than a frame some graph drives. Same two-string form as
 * MAHO_TRACE_SCOPE3 with the group left empty, because grouping answers "which collector drives
 * this", and a resident thread is driven by nobody.
 *
 *   MAHO_TRACE_SCOPE_LANE(GetThreadName(), "Task");
 */
#define MAHO_TRACE_SCOPE_LANE(First, Second) \
	::Maho::FScopedTracePair MAHO_TRACE_CONCAT(_MahoLane, __LINE__)("", First, Second)
