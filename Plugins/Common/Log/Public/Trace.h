#pragma once

#include <Core/BuildConfig.h>

// Trace -- CPU scope tracing for the profiling UI. It lives in the LOG plugin: the log plugin owns
// the process's diagnostics (the sinks + the `r.Trace` CVar), and the trace file is one of them, so
// the engine core keeps no tracing vocabulary at all.
//
// THREE MACROS, ONE SHAPE OF LINE. The whole family writes the SAME bar:
//
//   [tr] ts=<us> dur=<us> lane=<group> worker=<k> owner=<track> stage=<short> name=<label> [func=<fn>] tip=<text>
//
//   MAHO_TRACE_STAGE(StageType, Label, Tip)   FIRST statement of a stage body. Establishes the row:
//                                             lane = the frame TYPE's TraceGroupName()
//                                             owner = the frame TYPE's StaticName()
//                                             stage = typeid(StageType), shortened
//                                             and, on destruction, the flow hints (below).
//   MAHO_TRACE_SECTION(Label, Tip)            a hand-labelled section INSIDE a stage (or inside any
//                                             helper a stage calls): INHERITS lane/owner/stage from
//                                             the thread-local context the enclosing stage
//                                             established, and adds func=<short __FUNCTION__> so a
//                                             hover still says where the section lives.
//   MAHO_TRACE_SCOPE(Group, Tip)              a scope with a lane of its OWN -- resident threads and
//                                             free code that no frame drives. lane = owner = Group
//                                             (`GetThreadName()` for a FThreadedServer role), and
//                                             func=<short __FUNCTION__>.
//
// `lane` (the GROUP) is the architectural partition ("Engine", "FRender", "RHIServer") and becomes a
// collapsible Perfetto PROCESS, so the left panel reads as the architecture. `owner` is the frame
// that was running (a resident thread's own role name for the SCOPE form) and is the TRACK inside it.
// A track may only NEST bars or SEQUENCE them, never overlap them; per (lane, owner) is safe because
// the engine chains a frame's stages in order and keeps each stage serial against its own previous
// frame -- so the bars of one row nest by construction.
//
// `stage` is the machine key: the SHORT type name of the stage interface whose body opened the bar.
// `name` is the human label (a hand-written section name, or the stage's own label). `func` is where
// the bar lives. `tip` is optional.
//
// THE POOL'S OWN GROUP. Every bar is also filed, in the NATIVE file only, under lane "ThreadPool" with
// the thread that ran it as its row -- the engine runs every graph on one shared pool, so that is the
// one view where the stage work of every frame lines up by thread. Its bars name the frame they came
// from (their row no longer says it), and carry the edges that CROSS a thread: that group's rows ARE
// threads, so a handoff that stayed on one of them would just be a loop inside a row.
//
// THE FLOW HINTS -- what makes the dependency graph visible. A stage knows the edges it DECLARED
// (FFrameExtension::GetDependencies), so when its bar closes it emits one line per declared
// dependency, each describing where this bar's input came from:
//
//   [tr] flow ts=<this bar's start, us> lane=<group> owner=<track> stage=<short stage> \
//       from=<target frame name> fromStage=<short target stage> off=<frame offset>
//
// The viewer draws them as arrows between the two bars, which is what turns "these two bars are far
// apart" into "this one waited for that one". `off` is the declaration-time frame offset (0 = same
// frame, -1 = the previous frame).
//
// (ts, dur, lane, name) is one mechanical rewrite away from Chrome Trace Event Format's complete
// events, which is how Tools/trace_to_chrome.py turns this into a file that chrome://tracing and
// Perfetto load directly. `ts` is microseconds since the first traced call, on a MONOTONIC clock, so
// it is immune to wall-clock adjustments and comparable across threads.
//
// Two more environment switches, both read once:
//
//   MAHO_TRACE_MIN_US=<n>   duration floor for STAGE bars (default 0 = keep all). A stage bar is
//                           generated for EVERY stage of EVERY frame, so they are what makes a trace
//                           expensive; a SECTION/SCOPE is a human decision and is never filtered.
//                           Note the cost of filtering: rows are derived from events, so a frame
//                           whose every bar falls below the floor loses its row.
//   MAHO_TRACE_STAGES       (see Source/Private/Core/FrameGraph.cpp) enter/exit bracketing for
//                           the crash case, where the last stage ENTERED names the culprit.
//
// Note what this is NOT: a full event stream. Each scope costs two clock reads plus one line of
// buffer, so instrument the tens of scopes that matter, not every call in the frame.

#include <Core/FrameGraph.h>
#include <Core/TypeList.h>
#include <LogApi.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <typeinfo>

namespace Maho
{

/** Recording is on. The initial answer comes from MAHO_TRACE (read once while the process loads),
 *  and a runtime switch may flip it -- see TraceSetEnabled. Cheap enough for a hot path: one relaxed
 *  atomic load. */
MAHO_LOG_API bool TraceEnabled();

/** Flip recording at runtime. The `r.Trace` CVar drives this, and it cannot live in Core: Core must
 *  not depend on the ConsoleVariable plugin, so the plugin owns the CVar and calls in here. Turning
 *  it ON mid-run starts recording from that moment (the file is opened lazily on the first event);
 *  turning it OFF simply stops. Neither needs a restart, and the switch is per PROCESS. */
MAHO_LOG_API void TraceSetEnabled(bool bEnabled);

/** Monotonic microseconds since the trace origin (the first traced call). The `ts` we emit. */
MAHO_LOG_API std::uint64_t TraceNowMicros();

/** Duration floor for automatically generated STAGE bars (MAHO_TRACE_MIN_US, default 0 = all).
 *  Manual sections/scopes are never filtered -- see the note at the top of this header. */
MAHO_LOG_API std::uint64_t TraceTaskFloorMicros();

/** Flush every thread's pending events to the trace file. Called at exit; callable by hand so a
 *  host can snapshot mid-run (each call appends whatever is buffered). */
MAHO_LOG_API void TraceFlush();

#if MAHO_WITH_TRACE

/** The "this scope is not recording" sentinel for a trace scope's start stamp.
 *
 *  It MUST NOT be 0: the trace clock's origin is set by the first call, so the very first bar of a
 *  process legitimately starts at 0 -- and using 0 as the sentinel silently swallowed that bar's END
 *  (the bar never closed: `FLog Log init` showed up with no end time, and with no line in the text
 *  file either, because the text path also writes on END). */
inline constexpr std::uint64_t kNotTracing = ~std::uint64_t{0};

/** A slice of a STATIC string -- what every name in an event is (a literal, a stage's
 *  `type_info::name()`, `__FUNCTION__`). The shorten-and-strip-namespace work is a slice, never a
 *  copy: this runs on the emit path, tens of thousands of times a second once tracing is on. */
struct FStaticName
{
	const char* Data = "";
	std::size_t Size = 0;
};

/** RAII scope for MAHO_TRACE_STAGE -- the STAGE form.
 *
 *  It ESTABLISHES the row for everything that runs inside the stage body (nested sections, and any
 *  helper the stage calls), and it is the only form that knows the stage's IDENTITY: which stage
 *  interface it is and which frame it belongs to. On destruction it writes one bar, then one flow
 *  hint per dependency the frame declared for that stage (see the top of this header).
 *
 *  The definitions are out of line because the row/depth state is thread-local and owned by the
 *  .cpp. With tracing off this is a copy of a few pointers and a branch: `Start` stays 0 and the
 *  destructor does nothing. */
class MAHO_LOG_API FStageTrace
{
public:
	FStageTrace(const FFrameExtension* InFrame, const std::type_info& InStage, const char* InGroup,
		const char* InTrack, const char* InLabel, const char* InTip, const std::type_info* InPrevStage);
	~FStageTrace();

	FStageTrace(const FStageTrace&) = delete;
	FStageTrace& operator=(const FStageTrace&) = delete;
	FStageTrace(FStageTrace&&) = delete;
	FStageTrace& operator=(FStageTrace&&) = delete;

private:
	const FFrameExtension*                      Frame = nullptr;
	/** The edges THIS stage declared, or null. A pointer, not a copy: the frame is alive for the
	 *  whole stage call and never mutates its declarations after construction. */
	const std::vector<FFrameExtension::FEdge>*  Edges = nullptr;

	/** The stage the scheduler ran BEFORE this one inside the same frame: the STRUCTURAL in-frame
	 *  chain edge, deduced at compile time from the frame's own stage list (see StagePredecessorOf)
	 *  because a structural edge never appears in GetDependencies(). Null for a frame's first stage. */
	const std::type_info*                       PrevStage = nullptr;

	std::type_index Stage = std::type_index(typeid(void));
	const char*     Group = nullptr;
	const char*     Track = nullptr;
	const char*     Label = nullptr;
	const char*     Tip = nullptr;

	/** The row this bar replaced, given back on destruction (a worker thread runs node after node,
	 *  so the ambient row must not leak from one frame's stage into the next one's). */
	const char*     PreviousGroup = nullptr;
	const char*     PreviousTrack = nullptr;
	FStaticName     PreviousStage{};

	std::uint64_t Start = kNotTracing;
};

/** RAII scope for MAHO_TRACE_SCOPE -- a lane of its OWN.
 *
 *  For code no frame drives: a resident worker's task body, or a free function called from a
 *  context that has no row yet. `Group` is the lane AND the track (`GetThreadName()` for a
 *  FThreadedServer role), which is why a resident thread reads as one concept on the timeline.
 *
 *  It ESTABLISHES that row for its duration (a nested scope keeps landing on it) and restores the
 *  previous one on exit. With tracing off it is a pointer copy and a branch. */
class MAHO_LOG_API FScopeTrace
{
public:
	FScopeTrace(const char* InGroup, const char* InTip, const char* InFunc);
	~FScopeTrace();

	FScopeTrace(const FScopeTrace&) = delete;
	FScopeTrace& operator=(const FScopeTrace&) = delete;
	FScopeTrace(FScopeTrace&&) = delete;
	FScopeTrace& operator=(FScopeTrace&&) = delete;

private:
	const char* Group = nullptr;
	const char* Tip = nullptr;
	const char* Func = nullptr;

	const char*     PreviousGroup = nullptr;
	const char*     PreviousTrack = nullptr;
	FStaticName     PreviousStage{};

	std::uint64_t Start = kNotTracing;
};

/** RAII scope for MAHO_TRACE_SECTION -- a hand-labelled section INSIDE a stage.
 *
 *  It does NOT establish anything: it captures the row the enclosing MAHO_TRACE_STAGE (or
 *  MAHO_TRACE_SCOPE) put in place, so the section lands on the same row and nests inside the stage
 *  bar it belongs to. `Func` rides along in `func=` because the bar is named by a hand-written label
 *  rather than by its function -- a hover still says where the section lives.
 *
 *  With tracing off it is a few pointer copies and a branch. */
class MAHO_LOG_API FSectionTrace
{
public:
	FSectionTrace(const char* InLabel, const char* InTip, const char* InFunc);
	~FSectionTrace();

	FSectionTrace(const FSectionTrace&) = delete;
	FSectionTrace& operator=(const FSectionTrace&) = delete;
	FSectionTrace(FSectionTrace&&) = delete;
	FSectionTrace& operator=(FSectionTrace&&) = delete;

private:
	const char*   Label = nullptr;
	const char*   Tip = nullptr;
	const char*   Func = nullptr;

	/** The row it inherited, captured here so the bar cannot be attributed to a row that a nested
	 *  scope established in the meantime. */
	const char*   Group = nullptr;
	const char*   Track = nullptr;
	FStaticName   Stage{};

	std::uint64_t Start = kNotTracing;
};

#endif // MAHO_WITH_TRACE

} // namespace Maho

/** Unique name for the scope object (a plain counter, so it works at any scope). */
#define MAHO_TRACE_CONCAT_(A, B) A##B
#define MAHO_TRACE_CONCAT(A, B) MAHO_TRACE_CONCAT_(A, B)

/**
 * The stage the scheduler runs BEFORE `Stage` inside the frame that owns it, or null when nothing
 * does. Deduced at COMPILE TIME from the frame's own `IPipeline<TStages...>` base -- that list names
 * exactly the stages the frame mounts, in the order the scheduler chains them, so the predecessor in
 * it IS the in-frame chain edge.
 *
 * Why deduce instead of ask: a frame never DECLARES that edge (`FFrameBridge::Build` emits it as a
 * structural edge), so it cannot appear in GetDependencies() and the arrow would be missing from
 * every stage inside a frame. The list is the frame's own declaration, so this is not a guess.
 *
 * The fallback covers a host (FEngineBase) whose lifecycle stages are not a pipeline list: it simply
 * reports "no predecessor" instead of failing to compile.
 */
namespace Maho
{

template <typename TFrame, typename = void>
struct TFrameStageList
{
	using Type = TTypeList<>;
};

template <typename TFrame>
struct TFrameStageList<TFrame, std::void_t<typename TFrame::TStages>>
{
	using Type = typename TFrame::TStages;
};

template <typename... S>
inline const std::type_info* StagePredecessorIn(TTypeList<S...>, const std::type_info& Stage)
{
	const std::type_info* Types[sizeof...(S) == 0 ? 1 : sizeof...(S)] = { &typeid(S)... };
	for (std::size_t i = 0; i < sizeof...(S); ++i)
	{
		// type_info's operator== compares NAMES, not addresses: the macro and the frame's dispatch may
		// be in different modules, where one type has one type_info per module.
		if (*Types[i] == Stage)
		{
			return (i > 0) ? Types[i - 1] : nullptr;
		}
	}
	return nullptr;
}

template <typename TFrame>
inline const std::type_info* StagePredecessorOf(const TFrame*, const std::type_info& Stage)
{
	// The frame's own mounted-stage list: TStages comes from its IPipeline<...> base, and a type with
	// no such list (the host's lifecycle stages) reports an empty list -- no predecessor, no arrow.
	return StagePredecessorIn(typename TFrameStageList<TFrame>::Type{}, Stage);
}

} // namespace Maho

// -- the three macros -------------------------------------------------------------------
//
// IN SHIPPING THEY EXPAND TO NOTHING, which is the whole point of the capability macro: no scope
// object, no clock read, no string, no call -- the instrumentation is not in the binary at all. The
// price is the same one the check macros pay (Core/Fatal.h): a macro whose arguments carry side
// effects would lose them, so instrument with statements, not with expressions that must run.

#if MAHO_WITH_TRACE

/**
 * Trace a STAGE body. Put it as the FIRST statement of the body: the bar it opens is the stage's bar
 * (the node bar of the frame graph), and it must cover the whole body.
 *
 *   void FScene::BeginRender(FRender& R, FRenderContext& Frame)
 *   {
 *       MAHO_TRACE_STAGE(IBeginRender, "Scene frame head", "open the swapchain frame head");
 *       ...
 *   }
 *
 * `this` is the FRAME: the row comes from the frame type's TraceGroupName() / StaticName() (see
 * MAHO_DECLARE_FRAME / MAHO_DECLARE_FRAME_UNDER in Engine/Frame.h), the flow hints come from the
 * stage's own entry in GetDependencies(), and the in-frame chain arrow comes from the frame's stage
 * list (StagePredecessorOf). Nothing here is named by hand except the label and the tip.
 */
#define MAHO_TRACE_STAGE(StageType, Label, Tip)                                     \
	::Maho::FStageTrace MAHO_TRACE_CONCAT(_MahoStageTrace, __LINE__)(                \
		this, typeid(StageType), TraceGroupName(), StaticName().data(), Label, Tip,  \
		::Maho::StagePredecessorOf(this, typeid(StageType)))
/**
 * Trace one hand-labelled SECTION inside a stage (or inside any helper a stage calls). It inherits
 * the ambient row, so it lands on the frame that is running and nests inside that stage's bar.
 *
 * `__FUNCTION__` is the right name for a scope that IS the function's body, but it makes every scope
 * inside one function carry that same name -- several instruments in `FUIFeature::InitViews` would
 * all read "FUIFeature::InitViews", which is unreadable on a timeline (they are told apart only by
 * their tooltip). This form names each one for what it does; the function still rides along in
 * `func=`.
 *
 *   MAHO_TRACE_SECTION("InitViews: TranslateViews", "把注册的视图翻译成 ImGui 命令");
 */
#define MAHO_TRACE_SECTION(Label, Tip) \
	::Maho::FSectionTrace MAHO_TRACE_CONCAT(_MahoSectionTrace, __LINE__)(Label, Tip, __FUNCTION__)

/**
 * Trace a scope that carries a lane of its OWN -- a resident worker's task (a FThreadedServer role),
 * or free code no frame drives. `Group` becomes both the lane and the track, so a resident thread is
 * one row named by its role, and the bar is named by the function it lives in.
 *
 *   MAHO_TRACE_SCOPE(GetThreadName(), "wait for the previous frame's fence");
 */
#define MAHO_TRACE_SCOPE(Group, Tip) \
	::Maho::FScopeTrace MAHO_TRACE_CONCAT(_MahoScopeTrace, __LINE__)(Group, Tip, __FUNCTION__)

#else // MAHO_WITH_TRACE

/** Shipping: the instrumentation is not compiled. */
#define MAHO_TRACE_STAGE(StageType, Label, Tip) ((void)0)

/** Shipping: the instrumentation is not compiled. */
#define MAHO_TRACE_SECTION(Label, Tip) ((void)0)

/** Shipping: the instrumentation is not compiled. */
#define MAHO_TRACE_SCOPE(Group, Tip) ((void)0)

#endif // MAHO_WITH_TRACE
