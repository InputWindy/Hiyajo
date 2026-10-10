#pragma once

// FrameGraph -- the node scheduler.
//
// PROVIDES EXACTLY THREE THINGS:
//   1. Parallel execution of atomic tasks      (a node body runs once, on a pool thread)
//   2. Incremental DAG enqueue                 (Submit merges a batch into the live graph)
//   3. FEvent synchronization                  (per-instance completion flags + reverse tables)
//
// It does NOT model application semantics. In particular:
//   - it never interprets whether a dependency has "already run"; an edge exists iff both
//     endpoints exist. Execution state never enters edge construction.
//   - a dependency whose target instance does not exist makes the EDGE disappear (an edge
//     needs both endpoints). The node is simply one dependency lighter and may become ready
//     sooner. It does not need to know why: it only cares whether IT can run.
//   - it emits NO edge of its own: an edge exists iff it was handed one. It never invents
//     "the same stage one frame earlier", never picks a phase and never transitions one -- the
//     phase travels IN the task (FTaskKey::Phase), computed from the submitted ROUND (see
//     `Submit(batch, Frame)`), and the number the task carries is a node's identity, not an
//     ordering rule. The bridge above it resolves DECLARED edges only (see FFrameBridge): a stage
//     sequence means ordered stages and a stage's identity over time means its next frame waits
//     for its previous one, but both follow from a frame's OWN stage list -- so the frame declares
//     them (MAHO_FRAME_STAGE_CHAIN / MAHO_FRAME_SELF_EDGES, Engine/Frame.h) and the graph only ever
//     materializes what it is given.
//   - no notion of "init" or "shutdown". Those are CALLER conventions, not graph concepts:
//     the caller drains the graph, submits one batch, and the phase it picks for that batch
//     is just a ring index like any other.
//
// A node is an ATOMIC OPERATION: its completion is its only signal point. Completion is set
// by the SCHEDULER (once, after the body returns, exceptions included) -- never by the body,
// which therefore cannot reach the graph at all.
//
// All graph state is touched by ONE thread: the scheduler thread (this class is a
// FThreadedServer). Submit and completion notifications are FIFO commands. There is no lock
// on the graph state, and the scheduler thread must never block (I4).

#include <Core/Export.h>
#include <Core/ThreadedServer.h>
#include <Core/ThreadPool.h>
#include <Core/TypeList.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace Maho
{

#ifndef MAHO_FRAMES_IN_FLIGHT
#	define MAHO_FRAMES_IN_FLIGHT 3
#endif

// ── FRAME vs PHASE: the two vocabularies, and which side owns which ───────────
//
//   FRAME (a "round", `std::uint64_t`) -- the CALLER's vocabulary. It is monotonic, it only
//     ever moves forward, and it is what `Submit(batch, Frame)` takes. Where it comes from is
//     the application's business: the canonical source is `Platform::GetFrameIndex()`, whose
//     SINGLE writer is `FPlatform::IBeginFrame` (one fetch_add per driven frame, read once per
//     frame by the host and once per sequence drive). Core never queries it -- the number only
//     ever arrives as an argument.
//
//   PHASE (a ring index in [0, K)) -- the GRAPH's vocabulary, and it is COMPUTED HERE
//     (`PhaseOfFrame(Frame)` == `Frame % K`), never handed in. It is what a node's identity and
//     the admission predicate are made of, so the node table and the ring stay BOUNDED no matter
//     how long the process runs.
//
// The two are orthogonal and both are needed: the phase says WHICH SLOT, the frame says WHICH
// ROUND. Dropping either one loses a property the engine depends on (phase -> bounded container,
// frame -> "target = Frame + Offset" is meaningful and a wait can be expressed for any depth).
//
// ── THE WINDOW (admission == throttling; no Retire, no watermark) ─────────────────────────────
//
// `Submit(batch, Frame)` occupies ONE slot -- `P = Frame % K` -- and BLOCKS the caller until that
// slot is empty. So the number of permits is exactly K, and "K frames may be in flight" is not a
// convention anybody has to enforce: a frame cannot even be SUBMITTED while the frame K rounds
// earlier (which owns the same slot) still has a node running. The three consequences the rest of
// the file relies on:
//
//   1. K is the whole pipeline depth, and it is enforced by the graph rather than declared.
//   2. A frame LEAVES the window only when ALL of its nodes have completed -- the slot's in-flight
//      count is per node, so it reaches zero only after the last one is done.
//   3. Therefore a target frame OLDER than the oldest frame still inside the window is ALREADY
//      COMPLETE, and an edge naming it is satisfied by definition (see the bridge's offset rule).
//      That is why no per-frame record, no retire queue and no {Name,Stage} watermark is needed:
//      the blocking of the NEXT occupancy of a slot IS the completion proof.
//
// ── phase index space ─────────────────────────────────────────────────────────
//
// A phase IS a ring index: [0, K), where K is the PIPELINE DEPTH of the process (see
// GetFramesInFlight). That is the whole space -- there is
// no separate init or shutdown phase, because those are not graph concepts. A one-shot
// batch (install, uninstall) is submitted by the caller AFTER draining the graph, at
// whichever ring index it likes; since the graph is empty at that point, the index carries
// no pressure and the one-shot nodes simply keep that address for the rest of the run.
//
// A phase index is never reused for a different purpose, so a one-shot node keeps a STABLE
// ADDRESS while the frame ring advances. That is what lets a frame node name an
// install-time node as its dependency.
//
// Phase ORDER carries no meaning to the scheduler. All ordering is explicit edges.
//
// K IS A RUNTIME VALUE and it comes from the CONFIGURATION -- the CVar `r.FramesInFlight`, which is
// a `Config/DefaultEngine.ini` entry flagged ReadOnly (changing it requires a restart). Core is pure
// scaffolding and must not depend on the CVar plugin, so the VALUE arrives as a reader: the layer
// that owns the variable installs it here at static-init (`SetFramesInFlightSource`), i.e. before
// anything can ask. `GetFramesInFlight` asks ONCE and remembers the answer for the rest of the
// process; `MAHO_FRAMES_IN_FLIGHT` is what a process with no such layer (the test projects) gets.
//
// THERE IS NO SETTER ANY MORE. K used to be INJECTED by the host (`SetFramesInFlight`, called from
// the project entry point, read straight out of the ini) and SEALED by the first graph's
// construction -- two places that both claimed the value, and a seal that existed because the write
// came from outside the layer that consumes it. Now there is one authority (the CVar) and one
// reader (Core), and nothing can move K while the process runs.
MAHO_API void SetFramesInFlightSource(std::int32_t (*InSource)()) noexcept;
[[nodiscard]] MAHO_API std::int32_t GetFramesInFlight() noexcept;

inline bool IsValidPhase(std::int32_t Phase) noexcept
{
	return Phase >= 0 && Phase < GetFramesInFlight();
}

/** THE ring arithmetic, in ONE place: absolute frame number -> phase index (`Frame % K`).
 *  Everything that needs a phase goes through here (the graph's `Submit`, the bridge's
 *  resolution), so "which slot am I" cannot drift between the two. */
[[nodiscard]] inline std::int32_t PhaseOfFrame(std::uint64_t Frame) noexcept
{
	return static_cast<std::int32_t>(Frame % static_cast<std::uint64_t>(GetFramesInFlight()));
}

/** The phase of the frame `Offset` ROUNDS before `Frame` (`Offset <= 0`; a future frame is
 *  refused by the bridge, because a wait can only point backwards).
 *
 *  Computed in the signed domain on purpose: a naive `PhaseOfFrame(Frame + Offset)` would wrap a
 *  negative target into a huge unsigned value at the start of the run (Frame 0, Offset -1) and
 *  land on some unrelated slot -- which is precisely the "phase is not a frame" mistake the
 *  frame/phase split exists to make impossible. */
[[nodiscard]] inline std::int32_t PhaseOfShiftedFrame(std::uint64_t Frame, std::int32_t Offset) noexcept
{
	const std::int32_t K = GetFramesInFlight();
	const std::int32_t P = PhaseOfFrame(Frame);
	return static_cast<std::int32_t>(
		((static_cast<std::int64_t>(P) + Offset) % K + K) % K);
}

/** Is the frame `Offset` rounds before `Frame` still inside the active WINDOW (i.e. possibly
 *  still running)? Offsets are counted backwards, so `Offset <= -K` names a frame that the
 *  admission rule has already proven complete -- the edge is satisfied and is not emitted. */
[[nodiscard]] inline bool IsFrameInsideWindow(std::int32_t Offset) noexcept
{
	return Offset > -GetFramesInFlight();
}

/** The identity of the node body THIS THREAD is currently running --
 *  `<graph owner>:<frame name>::<stage>@<phase>` -- or an empty string when the thread is not inside
 *  one (`""`, never null).
 *
 *  IT EXISTS FOR THE HARD FAULT. A C++ exception thrown by a body is caught and reported by the graph
 *  (see FFrameGraph::Dispatch), but the failure that matters most here -- 0xC0000005, a body that
 *  dereferences something its own module already lost -- raises NOTHING a handler can catch and has no
 *  stack to walk afterwards: the crash reporter runs with the thread's frames already unwound to the
 *  fault. So the identity cannot be derived AT the crash; it must have been PUBLISHED while the body
 *  ran, and Core/Fatal.cpp prints exactly this string beside the crash (measured symptom it answers:
 *  "Maho CRASH (exception c0000005) at <module+off>" with no way to tell which of ~35 nodes it was).
 *
 *  A fixed thread-local buffer, not a string: the reader runs while the process is dying. */
[[nodiscard]] MAHO_API const char* GetCurrentGraphNode() noexcept;

/** An event is an INDEX into the atomic flag table -- not an object. */
using FEvent = std::uint32_t;
constexpr FEvent InvalidEvent = 0xFFFFFFFFu;

/** A node instance is an INDEX into the node table. */
using FNodeId = std::uint32_t;
constexpr FNodeId InvalidNodeId = 0xFFFFFFFFu;

namespace Detail
{
	/** Event index for (Id, phase). Computed, never allocated. The phase SPACE is a runtime
	 *  value now, so it is passed in rather than read from a constant. */
	inline FEvent EventOf(FNodeId Id, std::int32_t Phase, std::int32_t PhaseCount) noexcept
	{
		return static_cast<FEvent>(Id * static_cast<std::uint32_t>(PhaseCount)
			+ static_cast<std::uint32_t>(Phase));
	}
}

/**
 * A node instance's identity: the triple. This is also what a dependency points at --
 * a dependency IS the identity of its target, so there is no ring offset, no delta, and no
 * "same phase" sentinel to reason about.
 */
struct FTaskKey
{
	std::string_view Name{};   // the FRAME's StaticName() (MAHO_DECLARE_FRAME)

	/** The STAGE's static name -- `MAHO_DECLARE_STAGE`'s `StaticName()`, a string literal living in
	 *  the stage's own module. It used to be a `std::type_index` (`typeid(Stage)`), and the move off
	 *  RTTI is the point: a name is a compile-time constant that survives a module boundary and needs
	 *  no RTTI record, so the node table stays keyed and readable when RTTI is off, the identity can
	 *  be PRINTED without walking a vtable (see FTaskNode::NameSnapshot), and two frames' identities
	 *  compare as text. Invariant I3 applies unchanged: it must name static storage (a literal). */
	std::string_view Stage{};

	std::int32_t     Phase = 0;

	bool operator==(const FTaskKey& Other) const;
};

struct FTaskKeyHash
{
	std::size_t operator()(const FTaskKey& Key) const noexcept;
};

/** A dependency / block reference: the identity of the target instance. */
struct FDependency
{
	FTaskKey Target{};
};

/** A node declaration. The phase travels here, so the scheduler holds no phase state. */
struct FTask
{
	FTaskKey                 Key{};
	std::vector<FDependency> Dependencies;   // what I wait for (forward)
	std::vector<FDependency> Blocks;         // whom I block (reverse declaration)
	std::function<void()>    Closure;        // the payload; the scheduler never interprets it
};

// ── FFrameExtension: the named thing that DECLARES edges ────────────────────────────────
//
// A frame is authored ONCE and applied EVERY frame, so the edges it declares must be
// RELATIVE (a frame offset), while the graph only ever sees ABSOLUTE identities. Turning
// one into the other is the builder's job: it resolves `FrameOffset` against the frame
// number being submitted, and emits FTask::Dependencies as absolute FTaskKeys.
//
// This is why FFrameExtension lives here, next to the graph, and holds NO stage list and NO
// dispatch: it is a declaration of edges. (The engine's stage machinery -- a type-list of
// stage interfaces plus their Invoke dispatch -- sits on top of this and is none of Core's
// business. If FFrameExtension ever needs to know the stage list, it stops belonging in Core.)
//
// It is the PARALLEL replacement for the old layer base: the old one is left untouched
// until the whole pair has been proven in the sandbox, and then swapped in one block.

class FFrameExtension
{
public:
	virtual ~FFrameExtension() = default;

	/** Stable identity: the Name of every node this frame contributes. */
	[[nodiscard]] virtual std::string_view GetName() const = 0;

	/** Does this frame implement this stage, NAMED? The engine's frame macro answers it from the
	 *  frame's OWN `IPipeline<TStages...>` list (`MAHO_DECLARE_FRAME`, Engine/Frame.h), which IS the
	 *  frame's declaration of what it mounts -- so no `dynamic_cast` is involved and RTTI is not
	 *  needed. A frame that declares nothing (a hand-written frame outside the engine) answers false
	 *  here; the bridge's dispatch policy is what decides who gets a node, and it asks this. */
	[[nodiscard]] virtual bool ImplementsStage(std::string_view Stage) const
	{
		(void)Stage;
		return false;
	}

	/** The frame's OWN subobject for `Stage` (the stage interface it mounts), or nullptr when it does
	 *  not mount that stage. Generated by `MAHO_DECLARE_FRAME` from the frame's own stage list, so it
	 *  answers the same question `ImplementsStage` does -- and answers it with the POINTER the
	 *  dispatcher needs, which is what lets a stage call stop being a `dynamic_cast`.
	 *
	 *  WHY IT LIVES ON THE FRAME AND NOT IN THE DISPATCHER: the conversion from "some FFrameExtension"
	 *  to "that frame's IInit subobject" is a cross-cast, which only the frame's own type can perform --
	 *  both with `dynamic_cast` (RTTI, and a `typeid`-based lookup) and with the `static_cast` the
	 *  generated override uses internally (`static_cast<IInit*>(this)` from the most-derived type, which
	 *  is exactly the cast that is legal there and nowhere else). Returning `void*` is the erasure the
	 *  caller needs: it re-casts to the stage type it asked for, and the pointer it gets back points at
	 *  the right subobject of a MULTIPLY-INHERITED object, adjustment included.
	 *
	 *  A frame that declares nothing (no `MAHO_DECLARE_FRAME`) inherits this default and reports "I
	 *  mount nothing" -- the same answer `ImplementsStage` gives. Core knows the NAME, never the stage
	 *  list: the implementation is the engine's (Engine/Frame.h). */
	[[nodiscard]] virtual void* GetStageInstance(std::string_view Stage) noexcept
	{
		(void)Stage;
		return nullptr;
	}

	/** One declared edge: the target's identity parts, plus WHEN -- and WHICH WAY. */
	struct FEdge
	{
		std::string_view TargetName{};
		/** The target stage's STATIC NAME (see FTaskKey::Stage). */
		std::string_view TargetStage{};
		/** A RELATIVE FRAME OFFSET, always <= 0: 0 = this frame, -1 = the previous one, -K = one
		 *  full window back (i.e. the frame that last occupied this slot). Any offset <= 0 is
		 *  legal; `> 0` is REFUSED by the bridge with a diagnostic, because a wait can only point
		 *  backwards -- there is nothing to wait FOR in a frame that has not been submitted, and
		 *  accepting it would silently resolve to some other round's slot. */
		std::int32_t     FrameOffset = 0;
		/** The declaration's own direction, which the graph already knows (it is what `FDiagnostic`
		 *  reports and what the builder branches on) and which this struct used to discard: false =
		 *  `WaitFor` (the target runs FIRST, I wait for it), true = `BlockOn` (I run first, the target
		 *  is blocked by me). A consumer of the edge that must tell "input" from "output" -- the trace
		 *  drawing an arrow -- cannot recover it any other way. */
		bool             bReverse    = false;
	};

	// -- the minimal ability: declare edges (the sugar below is just spelling) ------

	/** Forward: my node at `MyStage` waits for these. */
	void AddDependency(std::string_view MyStage, FEdge Edge);

	/** Reverse: my node at `MyStage` blocks these. One-sided is enough -- the blocked
	 *  node needs to know nothing. */
	void AddDependent(std::string_view MyStage, FEdge Edge);

	[[nodiscard]] const std::unordered_map<std::string_view, std::vector<FEdge>>&
	GetDependencies() const { return Dependencies; }

	[[nodiscard]] const std::vector<std::pair<std::string_view, FEdge>>&
	GetDependents() const { return Dependents; }

	// -- sugar ---------------------------------------------------------------------
	//
	//   MyStage<IInitViews>().IsWaiting<FLog>().ForStage<IInit>();               // same frame
	//   MyStage<IInitViews>().IsWaiting<FLog>().ForStage<IInit>(-1);             // previous frame
	//   MyStage<IEditorInput>().IsBlocking<FUIFeature>().OnStage<IInitViews>();  // reverse
	//
	//   MyStage<IShutdown>().IsBlocking("FUIViewRegistry").OnStage<IShutdown>(); // by NAME
	//
	// The spelling deliberately mirrors the old layer DSL, so that the eventual swap is a
	// rename rather than a rewrite of every declaration site.
	//
	// The NAME-addressed forms exist for a consumer that must not name the producer's TYPE
	// (which would force a build dependency on an optional plugin). A name that matches no
	// frame in the frame set simply does not bind -- the bridge reports it (see FDiagnostic).
	//
	// ONE selector, ONE argument: the frame offset is a RELATIVE FRAME OFFSET (`<= 0`, including
	// `-K`) passed STRAIGHT TO THE SELECTOR -- there is no separate step, and no second spelling:
	//
	//   MyStage<IInitViews>().WaitFor<FLog>().OnStage<IInit>();       // FLog@IInit, THIS frame
	//   MyStage<IInitViews>().WaitFor<FLog>().OnStage<IInit>(-1);     // FLog@IInit, ONE frame back
	//   MyStage<IEditorInput>().BlockOn<FUIFeature>().OnStage<IInitViews>(-1);
	//   MyStage<IShutdown>().WaitFor("FUIViewRegistry").OnStage<IShutdown>(-1);
	//
	// `WaitFor`/`BlockOn` are the two directions; `OnStage`/`ForStage` are the selector they
	// share, both taking the offset (an offset of 0 is the same-frame case). `IsWaiting` /
	// `IsBlocking` remain as aliases of the direction. There used to be a SECOND spelling: a
	// shift-the-target step followed by a selector hard-wired to -1. One argument covers -1, -2
	// and -K alike, so that spelling was removed rather than kept as a second way to say it.

	template <typename TMyStage>
	struct TWaitScope
	{
		FFrameExtension& Self;
		explicit TWaitScope(FFrameExtension& InSelf) : Self(InSelf) {}

		template <typename TTargetFrame>
		struct TTarget
		{
			FFrameExtension&      Self;
			std::int32_t Offset = 0;
			explicit TTarget(FFrameExtension& InSelf) : Self(InSelf) {}

			/** The target's stage, `Offset` frames back (0 = this frame, -1 = the previous one,
			 *  -K = one full window back). */
			template <typename TTargetStage>
			TTarget& ForStage(std::int32_t Offset = 0)
			{
				Self.AddDependency(TMyStage::StaticName(),
					FEdge{ TTargetFrame::StaticName(), TTargetStage::StaticName(), Offset });
				return *this;
			}

			/** The same thing under the canonical name: the offset goes INTO the selector. */
			template <typename TTargetStage>
			TTarget& OnStage(std::int32_t Offset = 0)
			{
				return ForStage<TTargetStage>(Offset);
			}
		};

		/** Name-addressed target: the frame NAMED at declaration time. */
		struct TNamedTarget
		{
			FFrameExtension&          Self;
			std::string_view TargetName{};
			std::int32_t     Offset = 0;
			TNamedTarget(FFrameExtension& InSelf, std::string_view InName) : Self(InSelf), TargetName(InName) {}

			template <typename TTargetStage>
			TNamedTarget& ForStage(std::int32_t Offset = 0)
			{
				Self.AddDependency(TMyStage::StaticName(),
					FEdge{ TargetName, TTargetStage::StaticName(), Offset });
				return *this;
			}

			template <typename TTargetStage>
			TNamedTarget& OnStage(std::int32_t Offset = 0)
			{
				return ForStage<TTargetStage>(Offset);
			}
		};

		template <typename TTargetFrame>
		TTarget<TTargetFrame> IsWaiting() { return TTarget<TTargetFrame>(Self); }

		/** Name-addressed: `TargetName` must point at STATIC storage (a string literal). */
		TNamedTarget IsWaiting(std::string_view TargetName)
		{
			return TNamedTarget(Self, TargetName);
		}

		/** "My stage waits for this frame's stage" -- the canonical spelling of IsWaiting. */
		template <typename TTargetFrame>
		TTarget<TTargetFrame> WaitFor() { return TTarget<TTargetFrame>(Self); }

		/** Name-addressed WaitFor: for a producer whose TYPE must not be named. */
		TNamedTarget WaitFor(std::string_view TargetName)
		{
			return TNamedTarget(Self, TargetName);
		}
	};

	/** Same scope plus the reverse-declaration verbs. Deriving from TWaitScope is what lets
	 *  MyStage<S>() chain EITHER IsWaiting or IsBlocking -- one entry point, two verbs. */
	template <typename TMyStage>
	struct TBlockScope : TWaitScope<TMyStage>
	{
		FFrameExtension& Self;
		explicit TBlockScope(FFrameExtension& InSelf) : TWaitScope<TMyStage>(InSelf), Self(InSelf) {}

		template <typename TTargetFrame>
		struct TTarget
		{
			FFrameExtension&      Self;
			std::int32_t Offset = 0;
			explicit TTarget(FFrameExtension& InSelf) : Self(InSelf) {}

			/** `Offset` frames back (0 = this frame, -1 = the previous one, -K = one window back). */
			template <typename TTargetStage>
			TTarget& OnStage(std::int32_t Offset = 0)
			{
				Self.AddDependent(TMyStage::StaticName(),
					FEdge{ TTargetFrame::StaticName(), TTargetStage::StaticName(), Offset, /*bReverse=*/true });
				return *this;
			}
		};

		/** Name-addressed reverse target. */
		struct TNamedTarget
		{
			FFrameExtension&          Self;
			std::string_view TargetName{};
			std::int32_t     Offset = 0;
			TNamedTarget(FFrameExtension& InSelf, std::string_view InName) : Self(InSelf), TargetName(InName) {}

			template <typename TTargetStage>
			TNamedTarget& OnStage(std::int32_t Offset = 0)
			{
				Self.AddDependent(TMyStage::StaticName(),
					FEdge{ TargetName, TTargetStage::StaticName(), Offset, /*bReverse=*/true });
				return *this;
			}
		};

		template <typename TTargetFrame>
		TTarget<TTargetFrame> IsBlocking() { return TTarget<TTargetFrame>(Self); }

		/** Name-addressed: `TargetName` must point at STATIC storage (a string literal). */
		TNamedTarget IsBlocking(std::string_view TargetName)
		{
			return TNamedTarget(Self, TargetName);
		}

		/** "This frame's stage blocked by my stage" -- the canonical spelling of IsBlocking. */
		template <typename TTargetFrame>
		TTarget<TTargetFrame> BlockOn() { return TTarget<TTargetFrame>(Self); }

		/** Name-addressed BlockOn: for a consumer whose TYPE must not be named. */
		TNamedTarget BlockOn(std::string_view TargetName)
		{
			return TNamedTarget(Self, TargetName);
		}
	};

	template <typename TMyStage>
	TBlockScope<TMyStage> MyStage() { return TBlockScope<TMyStage>(*this); }

private:
	/** Keyed by the stage's STATIC NAME (`MAHO_DECLARE_STAGE`) -- the same identity a node carries. */
	std::unordered_map<std::string_view, std::vector<FEdge>> Dependencies;
	std::vector<std::pair<std::string_view, FEdge>>          Dependents;
};

// ── the bridge: FFrameExtension declarations -> ONE batch of FTask ──────────────────────
//
// The one place that knows the two things FFrameGraph deliberately does not:
//   - a stage SEQUENCE (which nodes exist at all)
//   - how a stage RUNS (the IDispatch policy -- Core cannot know what a stage is)
//
// It never touches the graph: Build() RETURNS a batch and the caller submits it, so the bridge
// has no admission or occupancy concerns and cannot get them wrong.
//
// It resolves the declaration-time RELATIVE offset into the graph's ABSOLUTE identity:
//
//     TargetKey = { TargetName, TargetStage, PhaseOf(Frame + FrameOffset) }
//
// and it never writes that back into the FFrameExtension: a declaration is authored once and re-applied
// every frame, so only the frame NUMBER being submitted can make it absolute.
//
// BUILD EMITS NOTHING OF ITS OWN. There used to be two STRUCTURAL edges invented here (an in-frame
// stage chain and a per-stage cross-frame self edge), on the argument that "a stage sequence means
// ordered stages, and only the bridge knows a sequence exists". True, and incomplete: both follow
// from a FRAME's OWN stage list, which only the frame has. So the frame DECLARES them
// (MAHO_FRAME_STAGE_CHAIN / MAHO_FRAME_SELF_EDGES, installed by MAHO_DECLARE_FRAME in
// Engine/Frame.h) and they arrive through the declaration pass like every other edge. Core keeps
// three jobs -- emit, resolve, admit -- and no ordering rule of its own.
//
// The two, for the record:
//
//   1. IN-FRAME CHAIN: each stage waits for the previous stage of the SAME frame. Declared over the
//      frame's own list, which is why a stage the frame does not mount cannot cut the chain in two
//      (the machinery this replaces chained the EMITTED nodes for the same reason).
//   2. CROSS-FRAME SELF EDGE: each node also waits for ITSELF one frame earlier. The same stage of
//      the same frame extension has the same job every frame, so two of its instances at once are
//      two writers of one stage's state. This is what makes "frame N+1 may overlap frame N" safe
//      for the stages themselves.
//
// WHAT EDGE 2 DOES NOT COVER, and must not be made to cover: two DIFFERENT stages of one frame
// extension that share state across a frame boundary (S1@N+1 while S3@N is still running). A frame
// extension that owns per-frame state is expected to hold K copies of it (one context per ring
// slot) -- that is what makes the overlap legal, and serializing a whole frame against its previous
// frame here would instead hide the missing copies inside the scheduler. The absence is measurable:
// with a single frame fence / one acquired swapchain index, this is a VkFence "simultaneously used
// in vkQueueSubmit and vkWaitForFences" from the validation layers -- the render layer's to fix.
//
// EDGE POLICY -- every DECLARED edge is EMITTED, and only a problem is REPORTED (FDiagnostic):
//
//   * Emitted even when the target is not in this batch: it may already be in the graph (a
//     one-shot node keeps its identity for the rest of the run, and the previous frame's nodes
//     came from the previous Build call). The graph's registry is scheduler-thread-only state
//     the bridge must not read -- and if the target truly does not exist, the graph makes the
//     edge disappear on its own.
//   * Reported only for an AUTHORING error THIS BATCH CAN PROVE: a target name outside the frame
//     set, a target frame that does not implement a stage that IS in this batch's sequence, a
//     declaration hanging on a stage the declaring frame does not implement, or a frame offset that
//     points FORWARD (`> 0`). A target that is merely at another PHASE is NOT reported -- that is what
//     every cross-frame edge looks like. Reporting it would fire on every frame of every pipeline.
//   * NOT emitted (and NOT reported) when the target STAGE is outside THIS batch's sequence: one
//     frame's stage list spans several sequences (init / tick / shutdown; a world system mounts
//     IProcessInput in one and IUpdate in another), so that declaration belongs to the batch whose
//     sequence holds the stage -- the mirror of the declarer-side rule that a declaration hanging on
//     a stage outside this sequence is skipped. ONLY the sequence decides this, because
//     `IDispatch::Implements` answers INSIDE a sequence: asked about a stage outside its own list it
//     says "no" by construction, so "the target frame owns that stage, in some other batch" is not a
//     question this batch can ask -- and a skip guarded by it never fired once (measured: 9 legal
//     cross-batch chain declarations reported as errors on every run). The cost, stated plainly: a
//     stage that NO frame mounts (a typo) is reported nowhere, because the batch that would own it
//     does not exist.
//   * NOT emitted at all when the offset reaches outside the WINDOW (`Offset <= -K`): such a
//     target frame is PROVEN complete (its slot had to drain before THIS frame could even be
//     submitted -- see "THE WINDOW" at the top of this header), so the edge is satisfied by
//     definition. This is what makes `-K`/`-2K` expressible without a watermark, a retire queue
//     or any per-frame bookkeeping: the frame is resolved to a phase and the phase to a slot,
//     and a slot can only be occupied by one round at a time.
//
// LIFETIME -- a closure from IDispatch::MakeClosure may capture the FFrameExtension (by address), the
// context (by pointer) and the ROUND (by value), so the two addresses must outlive the nodes they
// produced -- the host owns the frame, and the context IS the host. The IDispatch OBJECT itself need NOT:
// it is a per-batch stack local whose closures are self-contained (see TFrameDispatch), which is what
// lets a host pipeline frames.
//
// INVARIANT I3 -- FFrameExtension::GetName() feeds FTaskKey::Name, so it must return STATIC storage
// (MAHO_DECLARE_FRAME's StaticName() is a string literal, which satisfies it).

class FFrameBridge
{
public:
	/** Where a stage's body comes from. The engine implements this from its stage list and its
	 *  Invoke dispatch; Core only ever needs these two answers. */
	struct IDispatch
	{
		virtual ~IDispatch() = default;

		/** Does this frame implement this stage? A frame that does not gets NO node at all --
		 *  the empty-node pruning policy lives here. `Stage` is the stage's STATIC NAME (see
		 *  FTaskKey::Stage), so this answer is name-based and RTTI-free: the engine's implementation
		 *  (`TFrameDispatch`) asks `FFrameExtension::ImplementsStage`, which the frame macro answers
		 *  from the frame's own mounted-stage list. */
		[[nodiscard]] virtual bool Implements(const FFrameExtension& Frame, std::string_view Stage) const = 0;

		/** The body of (frame, stage) for round `Round`. Called once per emitted node.
		 *
		 *  `Round` is the batch's frame number, handed in HERE so the body can close over it BY VALUE:
		 *  the node's round is then a property of that one closure, not of a per-phase slot that the
		 *  next submission owning the phase will refill (see FFrameCall in Engine/Frame.h). */
		[[nodiscard]] virtual std::function<void()> MakeClosure(FFrameExtension& Frame,
			std::string_view Stage, std::uint64_t Round) = 0;
	};

	/** A declaration that looks wrong FROM THIS BATCH. Reporting never changes what is emitted:
	 *  the graph still decides whether the edge exists (see the edge policy above). */
	struct FDiagnostic
	{
		std::string_view Reason{};   // why it was reported
		std::string_view Frame{};    // the declaring frame
		std::string_view Stage{};    // the stage the declaration hangs on (its static name)
		std::string_view Target{};   // empty when the declaration itself is the problem
		std::string_view TargetStage{};
		std::int32_t     FrameOffset = 0;
		bool             bReverse = false;
	};

	struct FResult
	{
		std::vector<FTask>       Tasks;
		std::vector<FDiagnostic> Diagnostics;
	};

	/**
	 * Build one batch for frame number `Frame` (the ROUND, not a phase -- the phase is derived
	 * here via `PhaseOf`, so a caller never handles ring indices).
	 *
	 * `Frames` is the frame set to expand (the caller decides the set: an install batch may pass
	 * fewer frames than the loop does). `Stages` is the stage sequence, in order.
	 *
	 * A duplicate identity inside the batch (the same frame passed twice) is deliberately NOT
	 * checked here -- it is a structural property of a batch, which FFrameGraph::Submit rejects
	 * on its own.
	 */
	[[nodiscard]] static FResult Build(std::span<FFrameExtension* const> Frames,
		std::span<const std::string_view> Stages,
		std::uint64_t Frame,
		IDispatch& Dispatch);

	/** Absolute frame number -> phase index. Thin forwarder to `PhaseOfFrame` (the one place the
	 *  ring arithmetic lives); kept because the resolution here and the graph's `Submit` must
	 *  answer with the SAME number for the same frame. */
	[[nodiscard]] static std::int32_t PhaseOf(std::uint64_t Frame) noexcept;
};

/** Expand a stage TTypeList into the runtime view Build() consumes -- the one-line seam between
 *  the engine's compile-time stage sequence and the bridge's span. Each stage contributes its STATIC
 *  NAME (`MAHO_DECLARE_STAGE`), which is the identity the graph keys on from here on: a literal from
 *  the stage's own module, not a RTTI record. */
template <typename... TStages>
[[nodiscard]] std::array<std::string_view, sizeof...(TStages)> StageIndicesOf(TTypeList<TStages...>)
{
	return { TStages::StaticName()... };
}

/**
 * Runtime node instance. Persistent per identity:
 *   - bDispatched == false                -> fresh, or reaped
 *   - bDispatched == true, event not set  -> in flight
 *   - bDispatched == true, event set      -> completed
 * The event flag alone cannot separate "in flight" from "fresh": both read unset. That is
 * exactly what bDispatched exists for, and it is also the reuse predicate.
 *
 * Scheduler-thread-only. It does NOT need to be atomic (nothing outside the scheduler thread
 * reads it -- the caller's reuse decision goes through SlotInFlight) and it is not a copyability
 * hazard either, since Nodes is a deque populated by emplace_back. It is kept as
 * std::atomic<bool> purely for consistency with the rest of the instance state and to avoid
 * scattering a second access syntax through the implementation.
 */
struct FTaskNode
{
	FTaskKey             Key{};
	std::vector<FEvent>  Dependencies;      // in: events to wait for
	std::uint32_t        UnmetCount = 0;    // how many of them are not set yet
	std::vector<FNodeId> Successors;        // reverse: who waits for me
	FEvent               CompletionEvent = InvalidEvent;
	std::function<void()> Closure;

	/** The node's name as a COPY, taken on the scheduler thread when the batch was applied.
	 *
	 *  `Key.Name` is a string_view into the frame's own module (invariant I3: GetName() returns
	 *  static storage) -- and the ONE failure this diagnostic exists for is a node whose module was
	 *  ALREADY unloaded when its body ran (measured: `node threw: Access violation - no RTTI data!`,
	 *  i.e. a `dynamic_cast` on a freed instance whose vtable/RTTI image is gone). Reporting that
	 *  failure means PRINTING the name at exactly that moment, so the name must not be read through
	 *  the module: this copy is what makes the report survivable, and it is why `DescribeNode`
	 *  never touches `Key.Name`. */
	std::string          NameSnapshot;

	std::atomic<bool>    bDispatched{ false };
};

/**
 * The scheduler.
 *
 * How the caller is expected to drive it:
 *
 *   install  : drain (trivially empty) -> Submit(one batch, frame 0) -> drain
 *   loop     : per frame -> Submit(that frame's nodes, Frame) -> ...
 *   shutdown : drain -> Submit(one batch, frame 0) -> drain
 *
 * The admission inside Submit IS the per-frame throttle (see "THE WINDOW" at the top of this
 * header): the caller never asks whether a slot is idle before submitting -- submitting is what
 * waits. The one-shot batches are drained-then-submitted, so they never contend for a ring slot.
 *
 * Caveats worth knowing:
 *
 * A. An instance is only RESET when it is re-submitted (a new occupancy of its phase).
 *    Blindly resetting an instance whose body is still running would race its own state, so
 *    the RESET is guarded by the admission wait: Submit blocks until the phase named by this
 *    batch is idle, which is exactly the previous occupancy being over. The graph cannot make
 *    that wait anywhere else -- the scheduler thread must never block (it would stop seeing
 *    the very completions the wait depends on) -- which is why it lives in Submit, on the
 *    CALLER's thread.
 *
 * B. Producer before consumer, or the edge does not exist. A dependency whose target
 *    instance has not been submitted yet makes the edge disappear. The graph does not report
 *    it -- it cannot tell "not submitted yet" from "mis-spelled", and interpreting the
 *    caller's intent is not its job. The bridge, which knows the layer set and the stage
 *    list, is where such a declaration should be caught.
 *
 * C. Because only existing edges are ever waited on, every created instance is eventually
 *    dispatched -> there is no silent hang. The one exception is a cycle, which Submit
 *    rejects structurally.
 *
 * D. THE CALLER'S THREAD IS BLOCKED by admission, so Submit must not be called from work that
 *    must itself run on the pool to make progress: the slot being waited for is drained by
 *    POOL workers, and a wait issued from one occupies it. It is safe from the host loop
 *    (whose thread is not a pool worker) and from a collector's stage body only because the
 *    slot it waits for is driven by a DIFFERENT graph with its own scheduler thread. The
 *    audit of the engine's own Submit sites lives in Engine/FrameBuilder.h.
 */
class FFrameGraph : public FThreadedServer
{
public:
	explicit FFrameGraph(FThreadPool& InPool, FThreadPool::FLane InLane = FThreadPool::DefaultLane);
	~FFrameGraph() override;

	FFrameGraph(const FFrameGraph&) = delete;
	FFrameGraph& operator=(const FFrameGraph&) = delete;

	// ── submission ─────────────────────────────────────────────────────────

	/**
	 * Merge a batch of nodes into the live graph, as ROUND `Frame`.
	 *
	 * THE PHASE IS COMPUTED HERE (`PhaseOfFrame(Frame)` == `Frame % K`) and stamped onto every
	 * task's key, so the CALLER's vocabulary is the frame and the graph's is the phase: a batch
	 * handed in with stale/foreign phases cannot land in the wrong slot, and `Frame % K` is
	 * written in exactly one place. Node identity and the admission predicate stay keyed by
	 * PHASE, which is what keeps the node table and the ring bounded.
	 *
	 * ADMISSION (== the throttling that makes "K frames in flight" a fact rather than a hope):
	 * if the slot `Frame % K` still has instances in flight from a previous occupancy, Submit
	 * BLOCKS THE CALLING THREAD until that slot drains. That is the ring-reuse rule AND the
	 * window: the same blocking is what proves every frame `<= Frame - K` has completed. It is
	 * safe only because of the contract below.
	 *
	 * CONTRACT -- submit once per phase per occupancy. In the loop that means one Submit per
	 * frame; for a one-shot batch it means one Submit after draining. A second submit for the
	 * same occupancy would find the phase busy with ITS OWN nodes and block until they
	 * finish -- a stall, not silent corruption, which is why the loop must not do it.
	 *
	 * Structural validation only -- rejected (returning false, no side effects, reason in
	 * OutReason) when: the batch declares the same identity twice; the dependencies form a
	 * cycle; a phase index is out of range. Execution history is never consulted.
	 *
	 * A dependency whose target instance does not exist makes that edge disappear (see B);
	 * the batch is still accepted.
	 */
	[[nodiscard]] bool Submit(std::vector<FTask> Tasks, std::uint64_t Frame,
		std::string* OutReason = nullptr);

	/**
	 * Block the CALLER until every node it has SUBMITTED so far has completed.
	 *
	 * This is a submission FENCE (vkQueueSubmit + fence), not a "is the graph idle" query, and
	 * the distinction is the whole point: the phase in-flight counts rise when a node is
	 * DISPATCHED, so they read ZERO in the window between Submit() and the scheduler picking the
	 * command up. Waiting on those would return immediately and let the next frame dispatch
	 * alongside this one. The awaited count instead rises HERE, before the command is enqueued,
	 * and falls on the scheduler thread when a node completes -- so the fence cannot miss the
	 * work it is waiting for.
	 *
	 * NOT the same as the inherited FThreadedServer::Shutdown(), which stops the scheduler
	 * thread and drains its command queue but says nothing about nodes still running in the
	 * pool. The host must call this before unloading the module that owns the closures, and
	 * before submitting a one-shot batch.
	 *
	 * IT ALSO OBSERVES ITSELF: this is the call a teardown blocks on, so "it never returns" is a
	 * HANG -- the one failure mode that leaves no exception, no stack and no log line behind. Every
	 * `StallDumpSeconds` spent unsatisfied it prints, through the scheduler thread, which nodes are
	 * still outstanding and what each of them is waiting for (see DumpOutstanding). The wait itself
	 * is unchanged: the diagnostic never fakes a completion, it only makes the stall say WHO.
	 */
	void Wait();

	/**
	 * The collector whose frame set this graph drives. Purely diagnostic, and set ONCE before
	 * Initialize(): a collector's own stages are the top-level rows, and the frames IT drives are its
	 * sub-blocks. Left empty by the host graph, whose frames ARE the top level. The string must be
	 * static storage (a frame name from MAHO_DECLARE_FRAME is).
	 */
	void SetOwnerName(const char* InOwnerName) { OwnerName = InOwnerName; }

protected:
	/** The scheduler thread's name, and therefore the label of its row in a trace: this thread is
	 *  where dispatch, completion notification and the reaping runs, so it is a role worth being
	 *  able to tell apart from the render / IO / compile servers' rows. */
	[[nodiscard]] const char* GetThreadName() const override;

private:
	// The raw enqueue is not part of this class's surface: a caller must not inject arbitrary
	// commands into the scheduler thread. (Internal calls use the qualified name.)
	using FThreadedServer::Submit;

	// Flush() is a trap here: it drains the COMMAND QUEUE, not the nodes. A caller could mistake
	// it for "wait for the graph" -- Wait() is that.
	using FThreadedServer::Flush;

	// ---- everything below runs on the scheduler thread only (no locking) ----

	FNodeId GetOrCreateNodeId(const FTaskKey& Key);
	void    ApplySubmit(std::vector<FTask> Tasks);
	void    OnNodeCompleted(FNodeId Id);
	void    Dispatch(FNodeId Id);
	void    TryReap(FNodeId Id);
	bool    Validate(const std::vector<FTask>& Tasks, std::string& OutReason) const;

	// ── failure / stall diagnostics (see the note above Wait()) ────────────────

	/** A node's identity as text, WITHOUT its runtime state: `<frame>::<stage>@<phase>`, the name taken
	 *  from the node's own snapshot (see FTaskNode). This is the form published for the crash reporter. */
	[[nodiscard]] static std::string NodeIdentity(const FTaskNode& Node);

	/** The same identity plus the state a blocked fence needs to see: `[running]`/`[queued]` and the
	 *  count of dependencies that are still unset. Scheduler thread only. */
	[[nodiscard]] std::string DescribeNode(const FTaskNode& Node) const;

	/** Print one line per node whose occupancy has NOT settled, each followed by the dependencies of that
	 *  node that are still unset, spelled as node identities. That single dump answers both halves of
	 *  the question a stalled teardown raises: "which node never completed" and "what is it waiting
	 *  for that will never run". SCHEDULER THREAD ONLY -- it reads the node table. */
	void DumpOutstanding(const char* Why);

	/** The same dump, from a caller that is BLOCKED (a pool worker, the host thread or a node body):
	 *  marshaled to the scheduler thread, which is the only one allowed to read the node table. */
	void RequestOutstandingDump(const char* Why);

	/** A node body threw. Called from the POOL worker that caught it, so the identity + the in-flight
	 *  dump are MARSHALED to the scheduler thread (the node table is its state, and the report must be
	 *  queued BEFORE the completion notification so the dump describes the moment of the throw). */
	void ReportNodeFailure(FNodeId Id, const char* What);
	void ReportNodeFailureOnScheduler(FNodeId Id, std::string What);

	/** The interval a fence may stay unsatisfied before it dumps what is outstanding, and keeps
	 *  waiting. A HANG is the one failure that produces no evidence at all (no exception, no stack,
	 *  no log line), so the evidence has to be produced by whoever is blocked. */
	static constexpr std::int32_t StallDumpSeconds = 3;

	/** K, read ONCE on FIRST USE and a constant for the rest of the graph's life. EVERY K-deep table
	 *  below is sized from it (the slot counters here, the event table's phase stride), and it may not
	 *  be read at construction: the graph is a MEMBER of the frame that drives it, so it is built
	 *  before the plugins (and therefore the CVar) are loaded. Lazy is what makes "the configuration
	 *  decides K" true without a construction-order rule; `Depth == 0` means "not read yet".
	 *
	 *  The caller's thread reads it (Submit), the scheduler thread consumes it: it is published before
	 *  the first command is enqueued, so no synchronization beyond that hand-off is needed. */
	void EnsureDepth();

	[[nodiscard]] std::int32_t GetDepth() const noexcept { return Depth; }

	/** Admission used by Submit: block the CALLER until every phase named by this batch has
	 *  drained. This is where the ring-reuse wait lives -- Submit blocks, the scheduler
	 *  thread never does. */
	void WaitPhasesIdle(const std::vector<FTask>& Tasks);

	/** Internal observation -- NOT on the driving surface. A caller-visible query would be
	 *  meaningless anyway: Submit only enqueues, so a phase can still read idle before the
	 *  scheduler has dispatched anything. Kept for tests and diagnostics. */
	[[nodiscard]] bool        IsPhaseIdle(std::int32_t Phase) const;
	[[nodiscard]] std::size_t GetLiveNodeCount() const;

	/** Target instance declaration, or InvalidNodeId when it does not exist (in which
	 *  case the edge simply disappears). */
	FNodeId ResolveTarget(const FTaskKey& Target);

	void WireNode(FTaskNode& Node, const FTask& Decl);

	/** The pool the node bodies run in, and the LANE of it this graph owns.
	 *
	 *  The lane is what makes "the caller's own task is not in the count" true for this graph's
	 *  Wait(), even when several collectors share one worker set: a node body dispatched by THIS
	 *  graph is counted on THIS lane, so a stage body of the collector that owns this graph --
	 *  which was dispatched by the PARENT's graph, i.e. the parent's lane -- can block on this
	 *  lane's quiescence. See FThreadPool's header. */
	FThreadPool&      Pool;
	FThreadPool::FLane Lane;

	/** The name of the collector whose frame set this graph drives -- see SetOwnerName. Static
	 *  storage, empty for the host graph. */
	const char* OwnerName = "";

	// A deque, NOT a vector: FTaskNode holds a std::function and is not movable in a way a
	// vector's growth path needs, and a deque keeps references to existing elements valid --
	// which the wiring passes rely on.
	std::deque<FTaskNode>                               Nodes;       // index == FNodeId
	std::unordered_map<FTaskKey, FNodeId, FTaskKeyHash> Registry;    // exact key -> id
	std::deque<std::atomic_bool>                        EventStates; // index == FEvent

	/** The pipeline depth this graph runs at -- read once, on first use (see EnsureDepth). Every
	 *  phase-space table below is shaped by it, which is why it is the FIRST thing Submit resolves
	 *  and the only thing the scheduler thread needs to know about K. */
	std::int32_t Depth = 0;

	/** Per-phase in-flight counts: written by the scheduler, read by callers for waits.
	 *  Sized from `Depth` at first use (K is a runtime value -- see GetFramesInFlight), which is
	 *  why this is a vector and not an array. Empty == "K not read yet", i.e. nothing is in flight. */
	std::vector<std::atomic<std::uint32_t>> SlotInFlight;

	/** Nodes the caller has submitted that have not completed yet. The caller raises it in
	 *  Submit BEFORE the command is enqueued (which is what makes Wait() a correct fence --
	 *  see Wait) and the scheduler lowers it as each node completes. */
	std::atomic<std::uint32_t> AwaitingCompletion{ 0 };

	std::condition_variable                             SlotCv;
	mutable std::mutex                                  SlotMutex;   // pairs with SlotCv only
};

} // namespace Maho
