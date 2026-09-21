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
//     phase travels IN the task (FTaskKey::Phase) and the ring arithmetic (frame % K) belongs to
//     the caller. (The bridge does add structural edges -- see Core/FrameGraph.h's FFrameBridge:
//     a stage sequence means ordered stages, and a stage's identity over time means its next
//     frame waits for its previous one. Those are properties of a STAGE SEQUENCE, which the graph
//     deliberately knows nothing about; the graph only ever materializes what it is given.)
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

// ── phase index space ─────────────────────────────────────────────────────────
//
// A phase IS a ring index: [0, MAHO_FRAMES_IN_FLIGHT). That is the whole space -- there is
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

constexpr std::int32_t kPhaseCount = MAHO_FRAMES_IN_FLIGHT;

inline bool IsValidPhase(std::int32_t Phase) noexcept
{
	return Phase >= 0 && Phase < MAHO_FRAMES_IN_FLIGHT;
}

/** An event is an INDEX into the atomic flag table -- not an object. */
using FEvent = std::uint32_t;
constexpr FEvent InvalidEvent = 0xFFFFFFFFu;

/** A node instance is an INDEX into the node table. */
using FNodeId = std::uint32_t;
constexpr FNodeId InvalidNodeId = 0xFFFFFFFFu;

namespace Detail
{
	/** Event index for (Id, phase). Computed, never allocated. */
	inline FEvent EventOf(FNodeId Id, std::int32_t Phase) noexcept
	{
		return static_cast<FEvent>(Id * static_cast<std::uint32_t>(kPhaseCount)
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
	std::string_view Name{};
	std::type_index  Stage{ typeid(void) };
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

	/** The CPU-trace GROUP this frame -- and everything it drives -- is traced under: an
	 *  architectural partition ("Engine", "Render", "RHIServer"...), which is also the timeline LANE
	 *  its events are drawn on. Null (the default) means INHERIT: the collector that drives this frame
	 *  decides, and the engine's own top-level loop answers "Engine". A frame that IS a driver
	 *  declares its own -- FRender returns "Render" -- so its features inherit a lane instead of every
	 *  one of them having to name itself. */
	[[nodiscard]] virtual const char* GetTraceGroup() const { return nullptr; }

	/** One declared edge: the target's identity parts, plus WHEN. */
	struct FEdge
	{
		std::string_view TargetName{};
		std::type_index  TargetStage{ typeid(void) };
		std::int32_t     FrameOffset = 0;   // 0 = this frame, -1 = the previous one
	};

	// -- the minimal ability: declare edges (the sugar below is just spelling) ------

	/** Forward: my node at `MyStage` waits for these. */
	void AddDependency(std::type_index MyStage, FEdge Edge);

	/** Reverse: my node at `MyStage` blocks these. One-sided is enough -- the blocked
	 *  node needs to know nothing. */
	void AddDependent(std::type_index MyStage, FEdge Edge);

	[[nodiscard]] const std::unordered_map<std::type_index, std::vector<FEdge>>&
	GetDependencies() const { return Dependencies; }

	[[nodiscard]] const std::vector<std::pair<std::type_index, FEdge>>&
	GetDependents() const { return Dependents; }

	// -- sugar ---------------------------------------------------------------------
	//
	//   MyStage<IInitViews>().IsWaiting<FLog>().ForStage<IInit>();              // same frame
	//   MyStage<IInitViews>().WaitFor<FLog>().OnLastFrameStage<IInit>();        // previous frame
	//   MyStage<IEditorInput>().IsBlocking<FUIFeature>().OnStage<IInitViews>(); // reverse
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
	// The CANONICAL spelling puts the frame into the stage selector, so a cross-frame dependency
	// reads as one sentence and needs no LastFrame() step:
	//
	//   MyStage<IInitViews>().WaitFor<FLog>().OnStage<IInit>();            // FLog@IInit, THIS frame
	//   MyStage<IInitViews>().WaitFor<FLog>().OnLastFrameStage<IInit>();   // FLog@IInit, LAST frame
	//   MyStage<IEditorInput>().BlockOn<FUIFeature>().OnLastFrameStage<IInitViews>();
	//   MyStage<IShutdown>().WaitFor("FUIViewRegistry").OnLastFrameStage<IShutdown>();
	//
	// `WaitFor`/`BlockOn` are the two directions; `OnStage`/`OnLastFrameStage` are the selector
	// they share. IsWaiting/IsBlocking + LastFrame() + ForStage() remain as aliases.

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

			/** Shift the target back by one frame. Call BEFORE ForStage(). */
			TTarget& LastFrame() { Offset = -1; return *this; }

			template <typename TTargetStage>
			TTarget& ForStage()
			{
				Self.AddDependency(typeid(TMyStage),
					FEdge{ TTargetFrame::StaticName(), typeid(TTargetStage), Offset });
				return *this;
			}

			/** The target's stage in THIS frame. */
			template <typename TTargetStage>
			TTarget& OnStage()
			{
				Offset = 0;
				return ForStage<TTargetStage>();
			}

			/** The target's stage in the PREVIOUS frame -- the cross-frame dependency. */
			template <typename TTargetStage>
			TTarget& OnLastFrameStage()
			{
				Offset = -1;
				return ForStage<TTargetStage>();
			}
		};

		/** Name-addressed target: the frame NAMED at declaration time. */
		struct TNamedTarget
		{
			FFrameExtension&          Self;
			std::string_view TargetName{};
			std::int32_t     Offset = 0;
			TNamedTarget(FFrameExtension& InSelf, std::string_view InName) : Self(InSelf), TargetName(InName) {}

			TNamedTarget& LastFrame() { Offset = -1; return *this; }

			template <typename TTargetStage>
			TNamedTarget& ForStage()
			{
				Self.AddDependency(typeid(TMyStage),
					FEdge{ TargetName, typeid(TTargetStage), Offset });
				return *this;
			}

			template <typename TTargetStage>
			TNamedTarget& OnStage()
			{
				Offset = 0;
				return ForStage<TTargetStage>();
			}

			template <typename TTargetStage>
			TNamedTarget& OnLastFrameStage()
			{
				Offset = -1;
				return ForStage<TTargetStage>();
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

			/** Shift the target back by one frame. Call BEFORE OnStage(). */
			TTarget& LastFrame() { Offset = -1; return *this; }

			template <typename TTargetStage>
			TTarget& OnStage()
			{
				Self.AddDependent(typeid(TMyStage),
					FEdge{ TTargetFrame::StaticName(), typeid(TTargetStage), Offset });
				return *this;
			}

			/** The target's stage in the PREVIOUS frame (a cross-frame reverse declaration). */
			template <typename TTargetStage>
			TTarget& OnLastFrameStage()
			{
				Offset = -1;
				return OnStage<TTargetStage>();
			}
		};

		/** Name-addressed reverse target. */
		struct TNamedTarget
		{
			FFrameExtension&          Self;
			std::string_view TargetName{};
			std::int32_t     Offset = 0;
			TNamedTarget(FFrameExtension& InSelf, std::string_view InName) : Self(InSelf), TargetName(InName) {}

			TNamedTarget& LastFrame() { Offset = -1; return *this; }

			template <typename TTargetStage>
			TNamedTarget& OnStage()
			{
				Self.AddDependent(typeid(TMyStage),
					FEdge{ TargetName, typeid(TTargetStage), Offset });
				return *this;
			}

			template <typename TTargetStage>
			TNamedTarget& OnLastFrameStage()
			{
				Offset = -1;
				return OnStage<TTargetStage>();
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
	std::unordered_map<std::type_index, std::vector<FEdge>> Dependencies;
	std::vector<std::pair<std::type_index, FEdge>>          Dependents;
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
// THE STRUCTURAL EDGES -- what Build() emits on its own. Neither is a convenience: both are what
// a STAGE SEQUENCE means, and the bridge is the only thing that knows a sequence exists.
//
//   1. IN-FRAME CHAIN: each emitted node waits for the previously emitted node of the SAME frame.
//      Chained over the EMITTED nodes, so a skipped (unimplemented) stage cannot cut one frame's
//      chain into two independent halves.
//   2. CROSS-FRAME SELF EDGE: each node also waits for ITSELF one frame earlier. The same stage of
//      the same frame has the same job every frame, so two of its instances at once are two
//      writers of one stage's state. This is what makes "frame N+1 may overlap frame N" safe for
//      the stages themselves.
//
// WHAT EDGE 2 DOES NOT COVER, and must not be made to cover: two DIFFERENT stages of one frame
// extension that share state across a frame boundary (S1@N+1 while S3@N is still running). A frame
// extension that owns per-frame state is expected to hold MAHO_FRAMES_IN_FLIGHT copies of it --
// that is what makes the overlap legal, and serializing a whole frame against its previous frame
// here would instead hide the missing copies inside the scheduler. The absence is measurable:
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
//   * Reported only for an AUTHORING error: a target name outside the frame set, a target stage
//     outside the stage sequence, a target frame that does not implement the stage it is named
//     at, or a declaration hanging on a stage the declaring frame does not implement. A target
//     that is merely at another PHASE is NOT reported -- that is what every cross-frame edge
//     looks like. Reporting it would fire on every frame of every pipeline.
//
// LIFETIME -- a closure from IDispatch::MakeClosure may capture the FFrameExtension (by address) and the
// context (by pointer), so both must outlive the nodes they produced -- the host owns the frame, and the
// context IS the host. The IDispatch OBJECT itself need NOT: it is a per-batch stack local whose closures
// are self-contained (see TFrameDispatch), which is what lets a host pipeline frames.
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
		 *  the empty-node pruning policy lives here. */
		[[nodiscard]] virtual bool Implements(const FFrameExtension& Frame, std::type_index Stage) const = 0;

		/** The body of (frame, stage). Called once per emitted node. */
		[[nodiscard]] virtual std::function<void()> MakeClosure(FFrameExtension& Frame,
			std::type_index Stage) = 0;
	};

	/** A declaration that looks wrong FROM THIS BATCH. Reporting never changes what is emitted:
	 *  the graph still decides whether the edge exists (see the edge policy above). */
	struct FDiagnostic
	{
		std::string_view Reason{};   // why it was reported
		std::string_view Frame{};    // the declaring frame
		std::type_index  Stage{ typeid(void) };   // the stage the declaration hangs on
		std::string_view Target{};   // empty when the declaration itself is the problem
		std::type_index  TargetStage{ typeid(void) };
		std::int32_t     FrameOffset = 0;
		bool             bReverse = false;
	};

	struct FResult
	{
		std::vector<FTask>       Tasks;
		std::vector<FDiagnostic> Diagnostics;
	};

	/**
	 * Build one batch for frame number `Frame`.
	 *
	 * `Frames` is the frame set to expand (the caller decides the set: an install batch may pass
	 * fewer frames than the loop does). `Stages` is the stage sequence, in order.
	 *
	 * A duplicate identity inside the batch (the same frame passed twice) is deliberately NOT
	 * checked here -- it is a structural property of a batch, which FFrameGraph::Submit rejects
	 * on its own.
	 */
	[[nodiscard]] static FResult Build(std::span<FFrameExtension* const> Frames,
		std::span<const std::type_index> Stages,
		std::int32_t Frame,
		IDispatch& Dispatch);

	/** Absolute frame number -> phase index. The ring arithmetic is the BRIDGE's, never the
	 *  graph's (the graph only ever sees absolute identities). */
	[[nodiscard]] static std::int32_t PhaseOf(std::int32_t Frame) noexcept;
};

/** Expand a stage TTypeList into the runtime view Build() consumes -- the one-line seam between
 *  the engine's compile-time stage sequence and the bridge's span. */
template <typename... TStages>
[[nodiscard]] std::array<std::type_index, sizeof...(TStages)> StageIndicesOf(TTypeList<TStages...>)
{
	return { std::type_index(typeid(TStages))... };
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

	std::atomic<bool>    bDispatched{ false };
};

/**
 * The scheduler.
 *
 * How the caller is expected to drive it:
 *
 *   install  : drain (trivially empty) -> Submit(one batch, any phase) -> drain
 *   loop     : per frame -> WaitPhaseIdle(phase) -> Submit(that frame's nodes) -> ...
 *   shutdown : drain -> Submit(one batch, any phase) -> drain
 *
 * Only the LOOP pushes incrementally; the one-shot batches are drained-then-submitted, so
 * they never contend for a ring slot.
 *
 * Caveats worth knowing:
 *
 * A. An instance is only RESET when it is re-submitted (a new occupancy of its phase).
 *    Blindly resetting an instance whose body is still running would race its own state, so
 *    it is the CALLER's job not to do that: call WaitPhaseIdle(phase) when entering a
 *    phase's occupancy. The graph cannot do that wait itself -- the scheduler thread must
 *    never block (it would stop seeing the very completions the wait depends on), and only
 *    the caller knows whether a submit is a NEW occupancy or another batch of the SAME one
 *    (which is why Submit itself stays purely mechanical and may be called freely).
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
	 * Merge a batch of nodes into the live graph.
	 *
	 * ADMISSION: if any phase named by this batch still has instances in flight from a
	 * previous occupancy, Submit BLOCKS THE CALLING THREAD until that phase drains. That is
	 * the ring-reuse rule, and it is safe only because of the contract below.
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
	[[nodiscard]] bool Submit(std::vector<FTask> Tasks, std::string* OutReason = nullptr);

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
	 */
	void Wait();

	/**
	 * The collector whose frame set this graph drives, as the trace group for every node it
	 * dispatches (see Core/Profiler.h). Purely diagnostic, and set ONCE before Initialize():
	 * a collector's own stages are the top-level rows, and the frames IT drives are its
	 * sub-blocks, which is exactly the relation the group records. Left empty by the host
	 * graph, whose frames ARE the top level. The string must be static storage (a frame name
	 * from MAHO_DECLARE_FRAME is).
	 */
	void SetOwnerName(const char* InOwnerName) { OwnerName = InOwnerName; }

	/** The CPU-trace GROUP this graph's node bars are drawn under -- the LANE, and the fold key, for
	 *  everything this collector drives (see FFrameExtension::GetTraceGroup, which is where a frame
	 *  declares it). The owning builder resolves it once at creation (own declaration, else inherited
	 *  from the installer, else "Engine"), so it is always a registered group name. */
	void SetTraceGroup(const char* InTraceGroup) { TraceGroup = InTraceGroup; }

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

	/** Trace group for the nodes this graph dispatches. Static storage, empty for the host
	 *  graph -- see SetOwnerName. */
	const char* OwnerName = "";

	/** The GROUP (timeline lane) this graph's node bars are drawn under; empty = Global. Set once at
	 *  creation from the collector's resolved group -- see SetTraceGroup. */
	const char* TraceGroup = "";

	// A deque, NOT a vector: FTaskNode holds a std::function and is not movable in a way a
	// vector's growth path needs, and a deque keeps references to existing elements valid --
	// which the wiring passes rely on.
	std::deque<FTaskNode>                               Nodes;       // index == FNodeId
	std::unordered_map<FTaskKey, FNodeId, FTaskKeyHash> Registry;    // exact key -> id
	std::deque<std::atomic_bool>                        EventStates; // index == FEvent

	/** Per-phase in-flight counts: written by the scheduler, read by callers for waits. */
	std::array<std::atomic<std::uint32_t>, kPhaseCount> SlotInFlight{};

	/** Nodes the caller has submitted that have not completed yet. The caller raises it in
	 *  Submit BEFORE the command is enqueued (which is what makes Wait() a correct fence --
	 *  see Wait) and the scheduler lowers it as each node completes. */
	std::atomic<std::uint32_t> AwaitingCompletion{ 0 };

	std::condition_variable                             SlotCv;
	mutable std::mutex                                  SlotMutex;   // pairs with SlotCv only
};

} // namespace Maho
