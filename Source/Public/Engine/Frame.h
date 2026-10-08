#pragma once

// Frame -- the ENGINE side of the frame system.
//
// Core's FFrameExtension (Core/FrameGraph.h) is the DECLARATION layer: a name plus the edges it declares.
// It deliberately knows nothing about a stage sequence (see the note above it). This file is the
// other half -- the stage machinery that sits on top of it:
//
//   MAHO_DECLARE_FRAME(FrameType)   identity + DLL factory for a concrete frame, and the install of
//                                   the frame's two STRUCTURAL edge declarations (below)
//   MAHO_FRAME_STAGE_CHAIN(TFrame)  the in-frame stage chain: the frame's own mounted stages, in order
//   MAHO_FRAME_SELF_EDGES(TFrame)   the per-stage cross-frame self edge: each stage against its own
//                                   previous frame
//   Invoke<TStage, TContext>        the stage dispatch protocol: one full specialization per
//                                   (stage interface x context), declared by the context's owner
//                                   via MAHO_DECLARE_STAGE_DISPATCH
//   TFrameDispatch<TStages, TContext>
//                                   the FFrameBridge policy: which stages a frame implements,
//                                   and how to run one. THE ONLY place that knows a stage list.
//
// A concrete frame is therefore:
//
//   class FWorld : public FFrameExtension, public IPipeline<IInit, ITick, IShutdown>
//   {
//       MAHO_DECLARE_FRAME(FWorld);
//   public:
//       FWorld() { MyStage<IInit>().IsWaiting<FLog>().ForStage<IInit>(); }
//   };
//
// IPipeline<TStages...> is the ordered sequence (it already binds the stage interfaces, and it
// exposes them as TStages); FFrameExtension supplies identity + the edge declarations. Neither knows the
// other, which is why the pair is spelled as two bases rather than one.
//
// A frame that does not implement a stage contributes NO node for it (the bridge asks
// TFrameDispatch::Implements) -- there is no empty node, and therefore no no-op to schedule.
//
// ── THE TWO STRUCTURAL EDGES ARE DECLARED, NOT EMITTED ─────────────────────────────────────────
//
// A stage sequence means two things, and BOTH of them used to be invented by FFrameBridge::Build:
//
//   1. IN-FRAME CHAIN   -- a frame's mounted stages run in the order it lists them.
//   2. CROSS-FRAME SELF EDGE -- a stage of frame N+1 waits for the SAME stage of frame N (the stage
//      owns the same state every frame, so two of its instances at once are two writers).
//
// Neither is a property of "a batch": both follow from the frame's OWN stage list, which only the
// frame has. So the frame DECLARES them, exactly like every other edge it owns (a producer never
// enumerates its consumers; a frame never asks the scheduler for an ordering it knows itself), and
// the bridge keeps only three jobs: emit, resolve declared edges, and let the graph admit.
//
// The declarations are installed by MAHO_DECLARE_FRAME / MAHO_DECLARE_FRAME_UNDER themselves, through
// the two named macros below. WHY IN THE MACRO, and not hand-written in each frame's ctor (where the
// rest of a frame's edges live): this pair is not a taste decision -- FORGETTING it is a silent loss
// of ordering, and a silent loss of ordering is a race that reports itself thousands of frames later
// in the RHI. Installing it where identity is declared makes "every frame declares both" a property
// of the language instead of a property of the author's memory.
//
// Equivalence with the old emitted form is EXACT, and it is worth being precise about why:
//   * the chain expands over the frame's own list, so a stage the frame does not mount cannot cut it
//     (the old one chained the EMITTED nodes for the same reason); a pair whose target belongs to
//     ANOTHER batch's sequence (one frame's list spans init+tick+shutdown) is dropped by the bridge
//     exactly like the declarer-side cross-batch skip -- see FFrameBridge::Build.
//   * the self edge is one per mounted stage, which is one per EMITTED node (a stage is emitted iff
//     the frame implements it, and the frame's list IS what it implements).
// The batch a frame set builds is therefore identical, node for node and dependency for dependency.

#include <Core/Assembly.h>
#include <Core/FrameGraph.h>
#include <Core/Interface.h>
#include <Core/TypeList.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>

namespace Maho
{
namespace Detail
{

/** Declare the IN-FRAME CHAIN: for every ADJACENT PAIR (Prev, Next) of the frame's mounted stage
 *  list, `Next` waits for `Prev` in the same frame. Recursion on the list, so the pairs are
 *  (S1,S2), (S2,S3), ... and a list of one (or none) declares nothing. */
template <typename TFrame, typename TPrev, typename TCur, typename... TRest>
void DeclareStageChain(FFrameExtension& Self, TTypeList<TPrev, TCur, TRest...>)
{
	Self.template MyStage<TCur>().template WaitFor<TFrame>().template OnStage<TPrev>();
	if constexpr (sizeof...(TRest) > 0)
	{
		DeclareStageChain<TFrame>(Self, TTypeList<TCur, TRest...>{});
	}
}

template <typename TFrame>
void DeclareStageChain(FFrameExtension&, TTypeList<>)
{
}

/** A frame that mounts exactly ONE stage has no pair either -- the overload exists so the macro is
 *  usable for every shape of stage list instead of only for the two-or-more case. */
template <typename TFrame, typename TOnly>
void DeclareStageChain(FFrameExtension&, TTypeList<TOnly>)
{
}

/** Declare the CROSS-FRAME SELF EDGE: every mounted stage waits for ITSELF one frame earlier
 *  (`-1` = the previous round; `-K` would be one full window back and is already complete, so the
 *  bridge emits nothing for it -- the offset is spelled `-1` because "the previous round" is the
 *  smallest claim a stage's own state needs). */
template <typename TFrame, typename... TStages>
void DeclareSelfEdges(FFrameExtension& Self, TTypeList<TStages...>)
{
	(Self.template MyStage<TStages>().template WaitFor<TFrame>().template OnStage<TStages>(-1), ...);
}

} // namespace Detail
} // namespace Maho

/**
 * The two structural declarations of a frame (see the file comment above). Both expand over the
 * frame's OWN mounted stage list (`TFrameStageList<TFrame>`, Core/TypeList.h) -- a frame with no
 * list declares nothing instead of failing to compile. Call them from a frame's constructor; the
 * MAHO_DECLARE_FRAME macros below already do it for every frame, which is the spelling the engine
 * uses.
 *
 *   FMyFrame::FMyFrame() { MAHO_FRAME_STAGE_CHAIN(FMyFrame); MAHO_FRAME_SELF_EDGES(FMyFrame); }
 */
#define MAHO_FRAME_STAGE_CHAIN(TFrame) \
	::Maho::Detail::DeclareStageChain<TFrame>(*this, ::Maho::TFrameStageList<TFrame>::Type{})
#define MAHO_FRAME_SELF_EDGES(TFrame) \
	::Maho::Detail::DeclareSelfEdges<TFrame>(*this, ::Maho::TFrameStageList<TFrame>::Type{})

/**
 * Frame declaration sugar -- generates StaticName() + GetName() + TraceGroupName() + CreateFrame() +
 * GetModulePath() + the two structural edge declarations. CreateFrame/GetModulePath are NOT engine-frame-specific: every
 * FFrameExtension-derived frame that can be dynamically loaded carries the factory + module path.
 * Usage:
 *
 *   class FWorld : public FFrameExtension, public IPipeline<IInit, ITick> { MAHO_DECLARE_FRAME(FWorld); };
 *
 * The name comes from stringifying the type name (#FrameType); dependency declarations use
 * the same type deduction, so it is self-consistent, and it is a STRING LITERAL -- which is
 * what makes FTaskKey::Name (a string_view) safe (invariant I3).
 *
 * TraceGroupName() is the frame's TRACE LANE: the architectural partition its bars fold into (see
 * Trace.h). It is a name, not an identity -- "Engine" here, i.e. the engine's own loop, and the host
 * may override it by declaring the frame with MAHO_DECLARE_FRAME_UNDER instead. Nothing in the engine
 * reads it: the trace is the only consumer, and it is what lets every frames' bars be read as the
 * architecture rather than as an undifferentiated thread list.
 *
 * StructuralEdges is the one-shot member that installs MAHO_FRAME_STAGE_CHAIN + MAHO_FRAME_SELF_EDGES
 * per INSTANCE (see the file comment). It is a member, not a static, because the declarations are
 * stored ON the instance the bridge will expand.
 */
#define MAHO_DECLARE_FRAME(FrameType)                        \
public:                                                      \
	static constexpr std::string_view StaticName()           \
	{                                                        \
		return #FrameType;                                    \
	}                                                        \
	std::string_view GetName() const override                \
	{                                                        \
		return StaticName();                                  \
	}                                                        \
	static const char* TraceGroupName()                      \
	{                                                        \
		return "Engine";                                      \
	}                                                        \
	static Maho::FFrameExtension* CreateFrame()                       \
	{                                                        \
		return new FrameType();                                \
	}                                                        \
	static std::string GetModulePath()                       \
	{                                                        \
		return Maho::ApplyModuleExtension(#FrameType);         \
	}                                                        \
private:                                                     \
	/** Declares the frame's two STRUCTURAL edges at construction (order matters: an edge must exist
	 *  before the first Build expands the frame). Type name given by the macro above; the helper
	 *  calls are what MAHO_FRAME_STAGE_CHAIN / MAHO_FRAME_SELF_EDGES expand to, with the frame
	 *  handed in as the FFrameExtension base instead of `*this` (this struct is not the frame). */ \
	struct TStructuralEdges                                  \
	{                                                        \
		explicit TStructuralEdges(Maho::FFrameExtension& Self) \
		{                                                    \
			::Maho::Detail::DeclareStageChain<FrameType>(    \
				Self, ::Maho::TFrameStageList<FrameType>::Type{}); \
			::Maho::Detail::DeclareSelfEdges<FrameType>(     \
				Self, ::Maho::TFrameStageList<FrameType>::Type{}); \
		}                                                    \
	};                                                       \
	TStructuralEdges StructuralEdges{ *this };               \
public:

/**
 * The same, for a frame INSTALLED INTO another frame (a collector: FRender owns its features, the
 * editor owns its panels). Its bars belong to the collector's partition -- it is one of the things
 * that collector is made of -- so it inherits the collector's lane instead of carrying one of its own.
 *
 *   class FScene : public FFrameExtension, public IPipeline<IBeginRender, ...>
 *   {
 *       MAHO_DECLARE_FRAME_UNDER(FScene, FRender);   // bars land on the FRender lane
 *   };
 *
 * The group is the collector's StaticName() and is spelled here by STRINGIFYING the macro argument,
 * not by calling Collector::StaticName(): a frame header routinely CANNOT include the collector's
 * header (FScene.h would then drag Render.h into every consumer), and the two spellings are the same
 * string by construction -- StaticName() IS `#FrameType`. Naming the type is therefore all this
 * needs, and it needs no complete type at all.
 */
#define MAHO_DECLARE_FRAME_UNDER(FrameType, CollectorType)   \
public:                                                      \
	static constexpr std::string_view StaticName()           \
	{                                                        \
		return #FrameType;                                    \
	}                                                        \
	std::string_view GetName() const override                \
	{                                                        \
		return StaticName();                                  \
	}                                                        \
	static const char* TraceGroupName()                      \
	{                                                        \
		return #CollectorType;                                \
	}                                                        \
	static Maho::FFrameExtension* CreateFrame()                       \
	{                                                        \
		return new FrameType();                                \
	}                                                        \
	static std::string GetModulePath()                       \
	{                                                        \
		return Maho::ApplyModuleExtension(#FrameType);         \
	}                                                        \
private:                                                     \
	struct TStructuralEdges                                  \
	{                                                        \
		explicit TStructuralEdges(Maho::FFrameExtension& Self) \
		{                                                    \
			::Maho::Detail::DeclareStageChain<FrameType>(    \
				Self, ::Maho::TFrameStageList<FrameType>::Type{}); \
			::Maho::Detail::DeclareSelfEdges<FrameType>(     \
				Self, ::Maho::TFrameStageList<FrameType>::Type{}); \
		}                                                    \
	};                                                       \
	TStructuralEdges StructuralEdges{ *this };               \
public:


namespace Maho
{

/**
 * ONE stage call's context -- what the dispatcher hands a stage body and publishes for its duration.
 *
 * Two halves, and the split is the whole point:
 *
 *   * `Slot` is the context SLOT (`TContext::FContext*`), i.e. the per-PHASE state a collector keeps
 *     (`Frame % K`). It is shared by every node of one round and by the round that reuses the slot
 *     later, so anything long-lived about "this round" MUST NOT live in it.
 *   * `Frame` is THE ROUND THIS NODE WAS SUBMITTED WITH, carried BY VALUE from the node's own
 *     closure. It is immutable and belongs to exactly one node, so a node that runs late -- after
 *     the slot has been re-filled by a later submission -- still names its own round. Measured
 *     before this split: a consumer reading the round out of the slot saw `delta=+4` then `-2`
 *     between consecutive runs of its own stage, i.e. somebody else's round, and its single-cursor
 *     read then missed the whole round.
 */
struct FFrameCall
{
	void*         Slot  = nullptr;
	std::uint64_t Frame = 0;

	/** True when no stage call is running (the ambient context outside a stage body). */
	[[nodiscard]] bool IsBound() const noexcept { return Slot != nullptr; }
};

/**
 * Stage dispatch - a free function template specialized per (stage, context) pair. The bridge's
 * closure calls Invoke<TStage>(Frame, Context, Call) at run time; each stage interface gets full
 * specializations per context type (e.g. Invoke<IInit, FEngineBase> in Engine.h,
 * Invoke<IRender, FRender> in Render.h). A frame that does not implement the interface silently
 * skips -- which is a legal state, not an error (the bridge never even emits the node).
 */
template <typename TStage, typename TContext>
void Invoke(FFrameExtension* Frame, TContext& Context, FFrameCall Call);

/**
 * The frame context of the stage call running ON THIS THREAD, or nullptr outside a stage call.
 *
 * Exists because a stage body may call a helper that was NOT handed the context (the classic one is
 * "record this pass"): with frames pipelining, two frames' stage bodies run concurrently, so "the
 * frame I am part of" is a property of the CALL, not of the collector -- a helper that read it from
 * the dispatcher would attribute work to whichever frame the dispatcher happened to be setting up.
 * The dispatcher publishes it for exactly the duration of the call (FScopedFrameContext), so a
 * nested call (a one-shot batch inside a stage) sees its own and the outer one is restored after.
 */
MAHO_API void* GetCurrentFrameContext() noexcept;

/**
 * The ROUND of the stage call running ON THIS THREAD (0 outside a stage call): the frame number the
 * node was SUBMITTED with, published for the duration of the call alongside the slot above.
 *
 * It is a property of the NODE, not of the slot: with frames pipelining, several rounds' nodes
 * overlap, and a slot indexed by phase (`Frame % K`) is reused by the next round the moment its
 * admission clears -- so a value STORED in a slot is the round of whichever submission touched it
 * last, and a node that runs late reads somebody else's round. Carried by value from the closure
 * that was built for this one node (see TFrameDispatch::MakeClosureImpl), it cannot be overwritten.
 */
MAHO_API std::uint64_t GetCurrentStageFrame() noexcept;

/** Publishes the call context for the duration of one stage call (see the two getters above). */
class MAHO_API FScopedFrameContext
{
public:
	explicit FScopedFrameContext(FFrameCall InCall) noexcept;
	~FScopedFrameContext();

	FScopedFrameContext(const FScopedFrameContext&) = delete;
	FScopedFrameContext& operator=(const FScopedFrameContext&) = delete;

private:
	FFrameCall Previous;
};

/**
 * Stage dispatch specialization sugar - full-specializes Invoke<TStage, TContext> to
 * dynamic_cast the frame to CastType and call Method(Context, *Call.Slot). Each context type
 * (FEngineBase, FRender, ...) declares its own specializations for the stage interfaces it schedules.
 *
 *   MAHO_DECLARE_STAGE_DISPATCH(FEngineBase, IInit, IInit, Initialize)
 *   // => Invoke<IInit, FEngineBase>(Frame, Engine, {Slot, Round}) -> cast IInit -> Initialize(Engine, *Slot)
 *
 * THE TYPE-ERASURE BOUNDARY is here: the infrastructure carries the call as an FFrameCall whose SLOT
 * is a `void*` (see TFrameDispatch), and this macro is the ONE place it is turned back into a concrete
 * type -- which works because the expansion site (the scheduler's own header) has that nested type
 * complete. It is also why FFrameBuilder never has to name the type at all.
 *
 * It also PUBLISHES the call as the ambient one for the duration of the call
 * (GetCurrentFrameContext / GetCurrentStageFrame), which is what lets a helper reached from the stage
 * body -- one that was never handed the context -- attribute its work to the right frame AND read the
 * round that frame's node was submitted with.
 *
 * The cast is a cross-cast (the stage interface is a base of the concrete frame, not of FFrameExtension),
 * which is exactly why FFrameExtension must be polymorphic.
 */
#define MAHO_DECLARE_STAGE_DISPATCH(ContextType, StageType, CastType, Method) \
template <>                                                                    \
inline void Invoke<StageType, ContextType>(FFrameExtension* Frame, ContextType& Context, FFrameCall Call) \
{                                                                              \
	if (auto* S = dynamic_cast<CastType*>(Frame))                              \
	{                                                                          \
		FScopedFrameContext Ambient(Call);                                     \
		S->Method(Context, *static_cast<typename ContextType::FContext*>(Call.Slot)); \
	}                                                                          \
}

/** Context placeholder for stage sequences that need none. */
struct FEmptyContext
{
};

/**
 * The bridge policy for one stage sequence + one context: which stages a frame implements, and
 * how to run one. Deliberately the ONLY thing in the codebase that knows a stage list -- Core's
 * FFrameBridge takes it as an opaque, ordered span of type_indices.
 *
 * The closures it hands out are SELF-CONTAINED: they capture the frame's and the context's
 * addresses, never the dispatch object. That is load bearing, not tidiness -- a host that pipelines
 * frames outlives this stack local by a whole frame (see MakeClosureImpl).
 */
template <typename TStages, typename TContext = FEmptyContext>
class TFrameDispatch : public FFrameBridge::IDispatch
{
public:
	using FStages = TStages;

	explicit TFrameDispatch(TContext& InContext, void* InContextSlot)
		: Context(InContext)
		, FrameContext(InContextSlot)
	{
	}

	/** Does the frame implement this stage? A stage it does not implement gets NO node. */
	[[nodiscard]] bool Implements(const FFrameExtension& Frame, std::type_index Stage) const override
	{
		return ImplementsImpl(Frame, Stage, FStages{});
	}

	/** The stage's body: `Invoke<TStage, TContext>(Frame, Context, FFrameCall{Slot, Round})`, baked
	 *  at compile time. `Round` is the frame number the bridge is building this batch FOR: it becomes
	 *  the node's own immutable round, handed to the stage when the node RUNS -- however late that is
	 *  relative to the slot's reuse (see FFrameCall). */
	[[nodiscard]] std::function<void()> MakeClosure(FFrameExtension& Frame, std::type_index Stage,
		std::uint64_t Round) override
	{
		return MakeClosureImpl(Frame, Stage, Round, FStages{});
	}

	/** The stage sequence in the runtime form FFrameBridge::Build consumes. */
	[[nodiscard]] static constexpr auto StageIndices() noexcept
	{
		return StageIndicesOf(FStages{});
	}

private:
	template <typename TCurrent, typename... TRest>
	bool ImplementsImpl(const FFrameExtension& Frame, std::type_index Stage,
		TTypeList<TCurrent, TRest...>) const
	{
		if (Stage == std::type_index(typeid(TCurrent)))
		{
			return dynamic_cast<const TCurrent*>(&Frame) != nullptr;
		}
		if constexpr (sizeof...(TRest) > 0)
		{
			return ImplementsImpl(Frame, Stage, TTypeList<TRest...>{});
		}
		return false;
	}

	template <typename... TRest>
	bool ImplementsImpl(const FFrameExtension&, std::type_index, TTypeList<>) const
	{
		return false;
	}

	template <typename TCurrent, typename... TRest>
	std::function<void()> MakeClosureImpl(FFrameExtension& Frame, std::type_index Stage,
		std::uint64_t Round, TTypeList<TCurrent, TRest...>)
	{
		if (Stage == std::type_index(typeid(TCurrent)))
		{
			// Capture ADDRESSES, never `this`. A host PIPELINES frames -- FFrameBuilder::Execute
			// does not drain the batch it submits -- so a node routinely runs one whole frame
			// after the dispatch object that built it went out of scope. `this` would then be a
			// reused stack slot, and reading the Context member out of it hands the stage a
			// garbage context pointer (measured: FRender* arriving as 0x490).
			//
			// Both captured addresses are stable for the node's whole life: the frame lives in
			// the builder's owned set (freed only after a drained uninstall batch), the context is
			// the host object itself.
			//
			// `Round` is captured BY VALUE, and that is the fix for "a node read somebody else's
			// frame number": the SLOT is per phase and is refilled by the next submission that owns
			// that phase, so a round stored in it is not this node's round by the time a late node
			// runs. A value baked into this one closure cannot be overwritten by anybody.
			FFrameExtension* TargetFrame = &Frame;
			TContext* TargetContext = &Context;
			void* TargetSlot = FrameContext;
			return [TargetFrame, TargetContext, TargetSlot, Round]()
			{
				Invoke<TCurrent, TContext>(TargetFrame, *TargetContext,
					FFrameCall{ TargetSlot, Round });
			};
		}
		if constexpr (sizeof...(TRest) > 0)
		{
			return MakeClosureImpl(Frame, Stage, Round, TTypeList<TRest...>{});
		}
		return {};
	}

	template <typename... TRest>
	std::function<void()> MakeClosureImpl(FFrameExtension&, std::type_index, std::uint64_t, TTypeList<>)
	{
		return {};
	}

	TContext& Context;

	/** The SLOT's context, TYPE-ERASED (the per-phase state; NOT the round -- see FFrameCall). The
	 *  infrastructure only ever carries it; the one place it becomes a concrete type is the per-stage
	 *  dispatch specialization (see the macro above), which is why FFrameBuilder never names it. */
	void* FrameContext = nullptr;
};

} // namespace Maho
