#pragma once

// Frame -- the ENGINE side of the frame system.
//
// Core's FFrameExtension (Core/FrameGraph.h) is the DECLARATION layer: a name plus the edges it declares.
// It deliberately knows nothing about a stage sequence (see the note above it). This file is the
// other half -- the stage machinery that sits on top of it:
//
//   MAHO_DECLARE_STAGE(ExportTag, Stage, Method, Context)
//                                   ONE LINE -- the WHOLE stage interface: export tag + static identity
//                                   + a TYPED pure virtual entry (design D8 -- no erased entry, no
//                                   binding table). Written at NAMESPACE scope, not inside a class body.
//                                   The SLOT type is NOT a parameter: it is the COLLECTOR's own context
//                                   (`TCollectorContext<Context>::Type`), because a stage belongs to
//                                   exactly one collector's sequence
//   MAHO_DECLARE_FRAME(FrameType)   identity + DLL factory for a concrete frame, and the install of
//                                   the frame's two STRUCTURAL edge declarations (below)
//   MAHO_FRAME_STAGE_CHAIN(TFrame)  the in-frame stage chain: the frame's own mounted stages, in order
//   MAHO_FRAME_SELF_EDGES(TFrame)   the per-stage cross-frame self edge: each stage against its own
//                                   previous frame
//   MAHO_IMPLEMENT_STAGE(F, S, ...) ONE declaration per (frame, stage): the stage's resource access
//                                   table and the compile-time self-check (metadata ONLY -- the method
//                                   and its context are the interface's own declaration)
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
//       void Initialize(FEngineBase&, FEngineContext&) override;
//       void Tick(FEngineBase&, FEngineContext&) override;
//       void Shutdown(FEngineBase&, FEngineContext&) override;
//   };
//
// IPipeline<TStages...> is the ordered sequence, and it brings in the stage INTERFACES themselves
// (`public virtual TStageTypes...`), so a frame's overrides of their typed pure virtuals ARE its
// implementation -- "a frame mounts this stage" and "a frame answers this stage" are one fact (D8).
// FFrameExtension supplies identity + the edge declarations. Neither knows the other, which is why the
// pair is spelled as two bases rather than one.
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
#include <Core/ResourceName.h>
#include <Core/StageMetadata.h>
#include <Core/TypeList.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>

/**
 * Wrap a STAGE interface with EVERYTHING the protocol needs: its STATIC NAME (the identity every
 * node, edge and metadata row is keyed by) and its TYPED PURE VIRTUAL -- the ENTRY a stage call goes
 * through (design D8).
 *
 *   MAHO_DECLARE_STAGE(MAHO_API, IShutdown, Shutdown, FEngineBase)
 *   // ==>
 *   // class MAHO_API IShutdown
 *   // {
 *   // public:
 *   //     static constexpr std::string_view StaticName() noexcept;  // "IShutdown"
 *   //     static constexpr std::string_view MethodName() noexcept;  // "Shutdown"
 *   //     using TContextType = FEngineBase;  using TSlotType = FEngineContext;   // the trait's answer
 *   //     virtual void Shutdown(FEngineBase&, FEngineContext&) = 0;   // PURE, typed, unique name
 *   //     static void InvokeStage(IShutdown&, FEngineBase&, FEngineContext&);  // the trampoline
 *   //     virtual ~IShutdown() = default;
 *   // };
 *
 * ── WHY THE SLOT TYPE IS NOT A PARAMETER (it is the COLLECTOR's, read through a trait) ─────────
 *
 * `MAHO_DECLARE_STAGE` is written BEFORE the collector class exists -- it has to be: the collector
 * INHERITS the stage interfaces (`class FRender : public IBeginRender, ...`), so naming
 * `FRender::FContext` in a stage signature would be circular. The nested alias can therefore never be
 * read at a declaration point, and the slot type arrives through `TCollectorContext<Collector>`
 * (below) instead: a trait keyed on the collector, whose specializations live next to each context
 * type, so ONE fact (which collector drives this stage) yields one slot type with no second spelling
 * to drift. A trait specialization needs only a DECLARATION of the collector, which is exactly why
 * this works where the nested alias cannot.
 *
 * ── WHY THE MACRO IS THE WHOLE CLASS (and not an injection into a class body) ───────────────────
 *
 * A stage is a PROTOCOL, not an implementation: its identity, its entry, the two types that entry
 * names and its virtual destructor are ONE fact, and every stage that exists has to spell all of them
 * the same way. A macro that expands to the COMPLETE class makes that fact the only spelling there
 * is; a hand-written `class X { ... }` with the declaration injected inside would be a second spelling
 * that can drift (a stray non-virtual destructor, a missing `public:`, an extra member somebody
 * decided a "stage" needs). The EXPORT TAG is a parameter because it belongs to the module that OWNS
 * the interface (`MAHO_API`, `MAHO_RENDER_API`, `MAHO_GAMEWORLD_API`, ...) and to nothing else -- it
 * is the tag a consumer has to import, so it belongs on the class itself, and the macro is where the
 * class is written.
 *
 * ── WHY A STATIC NAME (and not `typeid`) ────────────────────────────────────────────────────────
 *
 * `typeid(T)` is a compile-time constant only for a NON-polymorphic type, and it disappears entirely
 * when RTTI is off -- and a stage interface IS polymorphic. A static name is a compile-time constant
 * unconditionally, it is readable in a dump, it is stable ACROSS A DLL BOUNDARY with no RTTI record
 * to share, and it is the exact counterpart of a frame's `StaticName()` (MAHO_DECLARE_FRAME below),
 * so the graph speaks one vocabulary for both halves of a node's identity: `{frame name, stage name}`.
 *
 * WHY NOT A VIRTUAL `GetName()`: a frame implements SEVERAL stage interfaces, and one overriding
 * virtual in the frame can only return ONE name for all of them (same signature, one slot) -- it
 * cannot say "I am IShutdown here and IRender there". A per-interface member has no such problem.
 *
 * ── WHY A TYPED PURE VIRTUAL (and not an erased entry) ──────────────────────────────────────────
 *
 * A `stage <-> sequence` pair is 1:1 (the host's sequence is driven by `FEngineBase`, `FRender` drives
 * the render sequence, `FGameWorld` the world's), so the CONTEXT a stage receives is a property of the
 * STAGE, not of who happens to implement it. The signature can therefore be TYPED -- no `void*
 * Collector`, no `FFrameCall` in the signature, no `static_cast` restoration at the override site:
 *
 *   1. `virtual void Method(Context&, TSlotType&) = 0;` is the DECLARATION of the entry (the slot type
 *      being the collector's own, see above), so
 *      "a frame mounts this stage" IMPLIES "the frame answers this entry" as a property of the
 *      language: a missing override leaves the class abstract, i.e. a compile error at
 *      `new FrameType()`. (An erased `void*` entry used to give the same guarantee, at the price of
 *      an extra macro in every frame's class body -- see MAHO_IMPLEMENT_STAGE_ENTRIES in git history.)
 *   2. The VIRTUAL NAME IS THE METHOD NAME, and it is UNIQUE PER STAGE by construction (`IShutdown::
 *      Shutdown`, `IExit::RequestExit`, ...). That is what lets one frame implement several stages
 *      (one override each, no ambiguity) while the DEFAULT-CONSTRUCTIBLE identity of a node stays a
 *      name. A single fixed virtual name is impossible for the same reason a virtual `GetName()` is.
 *   3. The frame inherits the interfaces it mounts (they arrive through its own `IPipeline<TStages...>`
 *      -- see Core/Interface.h), so `static_cast<Stage*>(this)` is a plain base conversion the
 *      dispatcher's side can no longer get wrong: `GetStageInstance(name)` hands back exactly that
 *      subobject (see FFrameExtension) and the call is a DIRECT VIRTUAL CALL, with no RTTI and no
 *      per-(frame, stage) binding table anywhere.
 *
 * ── THE TRAMPOLINE (`InvokeStage`) ──────────────────────────────────────────────────────────────
 *
 * The dispatcher walks an ordered list of stage TYPES, so it knows `TCurrent` but NOT the method each
 * stage names -- that name lives only here. `InvokeStage` is the one-line, macro-generated bridge:
 * a static member that takes the typed interface and calls its (virtual) entry. It is the whole of
 * the old `TStageBinding` + `TStageSignature` + `Invoke<TStage, TContext>` machinery, minus the
 * binding table and the RTTI. `TContextType`/`TSlotType` exist so the dispatcher can still CHECK (at
 * compile time) that the sequence it drives is the one this stage belongs to.
 *
 * The expansion IS the class, so it belongs at NAMESPACE scope where every other type declaration
 * goes (the engine's own stages sit in `namespace Maho`, the world's in `Maho::GameWorld`, ...). Every
 * member is public because the dispatcher -- which is not a friend of the interface -- has to reach
 * them.
 */
#define MAHO_DECLARE_STAGE(ExportTag, Stage, Method, Context)                            \
	class ExportTag Stage                                                                 \
	{                                                                                     \
	public:                                                                               \
		static constexpr std::string_view StaticName() noexcept { return #Stage; }         \
		static constexpr std::string_view MethodName() noexcept { return #Method; }        \
		using TContextType = Context;                                                      \
		using TSlotType    = typename ::Maho::TCollectorContext<Context>::Type;             \
		virtual void Method(Context&, TSlotType&) = 0;                                      \
		static void InvokeStage(Stage& Self, Context& C, TSlotType& S) { Self.Method(C, S); } \
		virtual ~Stage() = default;                                                        \
	};

/**
 * The NAME-ONLY form of a stage declaration: a COMPLETE class carrying `StaticName()` and nothing
 * else, written at NAMESPACE scope (or as a local class), exactly like `MAHO_DECLARE_STAGE`.
 *
 *   MAHO_DECLARE_STAGE_TAG(IStageOne)   // == class IStageOne { public: static constexpr ... };
 *
 * For a type that is used purely as an IDENTITY -- the graph's tests declare stage TAGS to address
 * nodes by name and never dispatch through them. Such a tag mounts no capability (and is never part
 * of a `TFrameDispatch` sequence), so a typed pure virtual would be an entry nobody can answer and the
 * frames built over it would be abstract for no reason. A REAL stage interface uses
 * `MAHO_DECLARE_STAGE`; this one is the escape hatch and the two must not be confused: only
 * `MAHO_DECLARE_STAGE` gives the "mounting implies implementing" guarantee.
 */
#define MAHO_DECLARE_STAGE_TAG(X)                                                    \
	class X                                                                              \
	{                                                                                    \
	public:                                                                              \
		static constexpr std::string_view StaticName() noexcept { return #X; }           \
	};

namespace Maho
{

/**
 * The COLLECTOR that drives a frame (D2'): `TFrameCollector<FScene>::Type` is `FRender`, and
 * `TFrameCollector<FLog>::Type` is `FEngineBase`. THE CONTEXT OF EVERY STAGE the frame mounts is
 * derived from this -- `TFrameCollector<Frame>::Type::FContext` is the slot the stage receives -- so
 * a frame's collector is not a taste decision: it is which stage sequence the frame is INSTALLED
 * INTO, i.e. the frame that calls `Install`/`InstallChildrenOf` for it.
 *
 * DECLARED BY THE FRAME ITSELF, in its own class body: `MAHO_DECLARE_FRAME` is `FEngineBase` (a host
 * sub-frame -- the majority) and `MAHO_DECLARE_FRAME_UNDER(Frame, Collector)` names the real parent
 * (a render feature is driven by `FRender`, an ECS system by `FGameWorld`, ...). Both record it as
 * the frame's own `TCollectorType`, which is all this trait reads -- so a frame header never has to
 * INCLUDE its collector's header (an alias names the type; completeness is only needed where the
 * entry's body actually casts, i.e. in the frame's own class body, which already includes it).
 *
 * A frame that declares NO collector has NO `Type` here -- deliberately: "no declaration" must be a
 * COMPILE ERROR at the first use, not a silent fall-back to the frame itself (that fall-back would
 * make every collector-subbordinate frame claim its own context, which is exactly backwards).
 */
template <typename TFrame, typename = void>
struct TFrameCollector
{
};

template <typename TFrame>
struct TFrameCollector<TFrame, std::void_t<typename TFrame::TCollectorType>>
{
	using Type = typename TFrame::TCollectorType;
};

/**
 * The SLOT CONTEXT of a collector: `TCollectorContext<FRender>::Type` is `FRenderContext`. THE ONE
 * mapping from "which collector" to "what its per-phase state is", read by the stage declaration and
 * by `FFrameBuilder` -- both of which need the answer while the COLLECTOR IS STILL INCOMPLETE.
 *
 * WHY THIS EXISTS AT ALL: a stage interface is declared BEFORE its collector class (the collector
 * INHERITS the interfaces, so the other order is circular), and a slot type has to be spelled in the
 * signature. The collector's own nested alias (`FRender::FContext`) can never be read there -- naming
 * a member of an incomplete class is not a thing -- while a TRAIT SPECIALIZATION needs nothing but a
 * forward declaration of the collector, which is exactly what the stage declaration point has.
 *
 * The specialization belongs NEXT TO the context type itself (`struct FRenderContext { ... };` in the
 * collector's own public header), so the collector and its slot context stay one fact in one file:
 *
 *   class FRender;                                   // forward declaration of the collector
 *   struct FRenderContext { ... };                   // the per-phase state
 *   template <> struct TCollectorContext<FRender> { using Type = FRenderContext; };
 *
 * A NESTED namespace needs the specialization at the ENCLOSING one (`Maho::GameWorld::FGameWorld` is
 * mapped from `namespace Maho`, next to the reopened namespace) -- an explicit specialization is only
 * allowed in a namespace enclosing the primary template's.
 *
 * A collector with NO specialization has NO `Type` here -- deliberately, and for the same reason
 * `TFrameCollector` above has none: a missing mapping must be a COMPILE ERROR at the first use (the
 * first stage declaration of that collector), never a silent fall back to a context nobody chose.
 */
template <typename TCollector>
struct TCollectorContext
{
};

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
 *     the slot has been re-filled later submission -- still names its own round. Measured
 *     before this split: a consumer reading the round out of the slot saw `delta=+4` then `-2`
 *     between consecutive runs of its own stage, i.e. somebody else's round, and its single-cursor
 *     read then missed the whole round.
 *
 * DECLARED HERE, ABOVE THE DISPATCH, because the dispatcher's signature NAMES it (`InvokeStageBound`
 * below takes one) -- a parameter type has to be declared at the point a function template is
 * DEFINED, not merely at the point it is instantiated: the name is not dependent, so it is looked up
 * where it is written.
 */
struct FFrameCall
{
	void*         Slot  = nullptr;
	std::uint64_t Frame = 0;

	/** True when no stage call is running (the ambient context outside a stage body). */
	[[nodiscard]] bool IsBound() const noexcept { return Slot != nullptr; }
};

/** Publishes the call context for the duration of one stage call (see the two getters below). Declared
 *  above the dispatch: the dispatcher defines a LOCAL of this type, which needs the complete type at
 *  the same non-dependent point `FFrameCall` needed its declaration. */
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

namespace Detail
{

/** Is `Stage` (a stage's static name) one of the stages in this list? Name comparison, so this needs
 *  neither RTTI nor a `dynamic_cast`: the list itself is the answer (a frame's `IPipeline<TStages...>`
 *  IS its declaration of what it mounts). Empty list => false. */
template <typename... TStages>
[[nodiscard]] bool ImplementsNamedStage(std::string_view Stage, TTypeList<TStages...>) noexcept
{
	return (... || (Stage == TStages::StaticName()));
}

/** Is `TStage` one of the stages in this list? Used by the declaration macro's self-check, BY TYPE
 *  (the list is a type list, so the question is a compile-time one -- unlike `ImplementsNamedStage`
 *  above, which answers a RUN-TIME question about a name). */
template <typename TList, typename TStage>
[[nodiscard]] constexpr bool MountsStage() noexcept
{
	return TContains_v<TList, TStage>;
}

/** The frame's OWN subobject for `Stage`, picked from the frame's mounted-stage list BY NAME.
 *
 *  This is the whole of "which stages does this frame mount" turned into a POINTER: the list is the
 *  frame's own `IPipeline<TStages...>`, so the `static_cast<TStage*>(Self)` below is the
 *  most-derived-to-base conversion that only the frame's own type can make -- legal here, and the
 *  reason the dispatcher can stop using `dynamic_cast` (see FFrameExtension::GetStageInstance). The
 *  fold covers the whole list; a stage that is not in it leaves the answer null instead of casting to
 *  something the frame is not. */
template <typename TFrame, typename... TStages>
[[nodiscard]] void* SelectStageInstance(TFrame* Self, std::string_view Stage, TTypeList<TStages...>) noexcept
{
	void* Instance = nullptr;
	((Stage == TStages::StaticName() ? void(Instance = static_cast<TStages*>(Self)) : void()), ...);
	return Instance;
}

/** The static registrant a `MAHO_IMPLEMENT_STAGE` expands into: it records the row in the graph-level
 *  access table at static-init time. A type with a constructor, rather than a free-floating call,
 *  because a DECLARATION has to be a declaration -- this way the expansion is one object at namespace
 *  scope, and its name is generated (see MAHO_DETAIL_JOIN) so several declarations can share a TU and
 *  a header can be included by many. */
struct FStageRegistrar
{
	FStageRegistrar(std::string_view Frame, std::string_view Stage, std::string_view Method,
		std::initializer_list<FResourceAccess> Accesses)
	{
		RegisterStageAccess(Frame, Stage, Method, Accesses);
	}
};

/** A stage method's SIGNATURE is the STAGE's own declaration (`MAHO_DECLARE_STAGE`, i.e. the class it
 *  expands to), so there is nothing to take apart here any more: the context and the slot
 *  type are named by the interface itself (`TContextType` / `TSlotType`), and the dispatcher reads
 *  them straight off the stage type. The old `TStageSignature` trait (which recovered them from a
 *  bound method pointer) retired with the `(frame, stage)` binding table -- see D8. */

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
	/** THE COLLECTOR THAT DRIVES THIS FRAME (D2'): a host sub-frame is driven by the engine host, so \
	 *  every stage it mounts is handed an `FEngineBase&` + its context (see TFrameCollector). A frame \
	 *  that is driven by another frame instead declares `MAHO_DECLARE_FRAME_UNDER`. */               \
	using TCollectorType = ::Maho::FEngineBase;               \
	/** Which stages this frame mounts, answered BY NAME (see ImplementsNamedStage): the frame's own
	 *  `IPipeline<TStages...>` list IS the answer, so the scheduler needs neither a `dynamic_cast`
	 *  nor a RTTI record -- and a stage the frame does not mount cannot be reported as implemented
	 *  by accident. */                                                                   \
	bool ImplementsStage(std::string_view Stage) const override                           \
	{                                                                                     \
		return ::Maho::Detail::ImplementsNamedStage(Stage,                                \
			::Maho::TFrameStageList<FrameType>::Type{});                                  \
	}                                                                                     \
	/** The frame's OWN stage subobject for `Stage`, or null (see FFrameExtension). Answers from the
	 *  same list `ImplementsStage` does, with the pointer the dispatcher needs: `static_cast<IInit*>(this)`
	 *  is legal here (this IS the most-derived frame) and nowhere else -- which is what retires the
	 *  `dynamic_cast` the dispatcher used to perform, and with it the RTTI dependency. */  \
	void* GetStageInstance(std::string_view Stage) noexcept override                       \
	{                                                                                     \
		return ::Maho::Detail::SelectStageInstance(this, Stage,                            \
			::Maho::TFrameStageList<FrameType>::Type{});                                   \
	}                                                                                     \
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
	/** THE COLLECTOR THAT DRIVES THIS FRAME (D2'): the frame it is INSTALLED INTO -- `FRender` for a \
	 *  render feature, `FGameWorld` for an ECS system, `FExampleEditor` for an editor panel. Every \
	 *  stage this frame mounts is handed that collector's `&` + its context (see TFrameCollector); \
	 *  it is NOT the frame itself (a collector is driven BY its own parent). */                      \
	using TCollectorType = CollectorType;                     \
	/** See MAHO_DECLARE_FRAME: the frame's own mounted-stage list answers, by name. */ \
	bool ImplementsStage(std::string_view Stage) const override                    \
	{                                                                              \
		return ::Maho::Detail::ImplementsNamedStage(Stage,                          \
			::Maho::TFrameStageList<FrameType>::Type{});                             \
	}                                                                              \
	/** See MAHO_DECLARE_FRAME: the same list, answered with the stage's subobject. */ \
	void* GetStageInstance(std::string_view Stage) noexcept override                \
	{                                                                              \
		return ::Maho::Detail::SelectStageInstance(this, Stage,                      \
			::Maho::TFrameStageList<FrameType>::Type{});                             \
	}                                                                              \
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
 * STAGE DISPATCH -- what a batch's closure calls, and it is now a DIRECT VIRTUAL CALL (D8).
 *
 * The old shape was a free function `Invoke<TStage, TContext>(Frame, Context, FFrameCall)` that went
 * through a `(frame, stage)` binding table (`TStageBinding`) and a `static_cast` restoration of the
 * erased context. All of that is GONE, because none of it is needed any more:
 *
 *   * the ENTRY is the stage's own typed pure virtual (`MAHO_DECLARE_STAGE`), so the interface declares
 *     the method name and the context types and no table has to say what a stage is called;
 *   * the frame reports its own subobject for a stage by NAME (`FFrameExtension::GetStageInstance`),
 *     and that pointer is the typed interface -- `TCurrent::InvokeStage(*Instance, Context, Slot)` is
 *     a plain virtual call through it;
 *   * a frame that does not mount the stage contributes no node (the bridge asks `Implements` first),
 *     so "the frame has nothing to answer with" is not a case to handle -- and it is a legal state,
 *     not an error (the stage call simply does not happen).
 *
 * The dispatcher's own body is `TFrameDispatch::MakeClosureImpl` below, i.e. the one place that still
 * knows an ordered stage list.
 */

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

/** Token pasting in two steps, so the `__COUNTER__` inside MAHO_IMPLEMENT_STAGE is EXPANDED before it
 *  is pasted (the one-step form would paste the literal text `__COUNTER__`). */
#define MAHO_DETAIL_JOIN_IMPL(A, B) A##B
#define MAHO_DETAIL_JOIN(A, B) MAHO_DETAIL_JOIN_IMPL(A, B)

/**
 * ONE declaration per `(frame, stage)`: WHAT THAT STAGE TOUCHES, plus the compile-time check that keeps
 * it honest. Written in the frame's own .cpp, next to the method body (D8: the macro carries METADATA
 * ONLY -- the method and its context are declared by the stage interface itself, so there is nothing
 * to bind here any more).
 *
 *   MAHO_IMPLEMENT_STAGE(FGameWorld, IShutdown,
 *       MAHO_ACCESS_DESTROY(GameWorld::FComponentPools), MAHO_ACCESS_READ(FLog))
 *
 * It expands two things and nothing else:
 *
 *   1. THE SELF-CHECK -- `static_assert(std::is_base_of_v<Stage, Frame>)`: the stage interface must be
 *      a BASE of the frame, i.e. the frame must mount it (its `IPipeline<TStages...>` list is what
 *      brings the interface in). That is the "`IPipeline` 挂载 ⊆ 继承" half of the D8 invariant; the
 *      other half ("继承 ⇒ 必须实现") is given for free by the stage's PURE virtual, so together they
 *      say 挂载 == 实现.
 *   2. THE TABLE ROW -- a namespace-scope registrant that records
 *      `(frame name, stage name, method name, [ {resource, mode, ordering} ] )` in Core's graph-level
 *      table (Core/StageMetadata.h). Registration happens at static-init time and is idempotent,
 *      because a declaration in a header is expanded once per translation unit. The METHOD NAME comes
 *      from the interface (`Stage::MethodName()`), not from a macro argument: the stage declares its
 *      own entry name, and the frame's override necessarily has that name.
 *
 * ── `Frame` IS ALWAYS A REAL FRAME ──────────────────────────────────────────────────────────────
 *
 * The first argument names the CONCRETE frame whose implementation this row describes -- `FLog`,
 * `FPlatform`, `FRender`, `FGameWorld`, `FScriptSystem`, a render feature, an ECS system:
 *
 *   MAHO_IMPLEMENT_STAGE(FGameWorld, IShutdown, MAHO_ACCESS_DESTROY(...))   // FGameWorld's own
 *
 * It is NOT a stage. A stage is not a frame, and it is implemented by MANY frames, so a row that
 * named a stage on both sides (`MAHO_IMPLEMENT_STAGE(IInit, IInit)`) said nothing a consumer could
 * use -- and the protocol information it claimed to register (name, method, context) is already
 * carried by the stage interface's own declaration (`MAHO_DECLARE_STAGE`), which is exactly what the
 * graph, the dispatcher and the metadata row read it off. Such rows are gone; a type that is only an
 * IDENTITY is declared with `MAHO_DECLARE_STAGE_TAG` and registers no row at all.
 */
#define MAHO_IMPLEMENT_STAGE(Frame, Stage, ...)                                                   \
	static_assert(::std::is_base_of_v<Stage, Frame>,                                              \
		"MAHO_IMPLEMENT_STAGE: this frame does not mount this stage -- the stage interface must be " \
		"a base of the frame, i.e. part of its IPipeline<...> stage list (and the first argument "     \
		"must be a real FRAME, never a stage interface: a stage registers no row of its own)");       \
	[[maybe_unused]] const ::Maho::Detail::FStageRegistrar                                         \
		MAHO_DETAIL_JOIN(RegisterStage_, __COUNTER__){ Frame::StaticName(), Stage::StaticName(),     \
			Stage::MethodName(), { __VA_ARGS__ } };

/** Context placeholder for stage sequences that need none. */
struct FEmptyContext
{
};

/** Its own collector mapping, so `TFrameDispatch<TStages>` (the default second argument below) still
 *  names a slot type -- the placeholder IS both the collector-ish identity and the context. */
template <>
struct TCollectorContext<FEmptyContext>
{
	using Type = FEmptyContext;
};

/**
 * The bridge policy for one stage sequence + one context: which stages a frame implements, and
 * how to run one. Deliberately the ONLY thing in the codebase that knows a stage list -- Core's
 * FFrameBridge takes it as an opaque, ordered span of stage NAMES (each stage's `StaticName()`).
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

	/** `TContext` is the COLLECTOR (the frame that drives the sequence, never a stage), and the slot
	 *  type is DERIVED from it (see TCollectorContext) -- the same fact the stage signatures spell.
	 *  Until this was a trait the collector could not name it at all, which is why the channel used to
	 *  be a `void*`; now the two ends are the same type by construction and no cast remains. */
	using FSlotType = typename TCollectorContext<TContext>::Type;

	explicit TFrameDispatch(TContext& InContext, FSlotType& InContextSlot)
		: Context(InContext)
		, FrameSlot(InContextSlot)
	{
	}

	/** Does the frame implement this stage? A stage it does not implement gets NO node.
	 *
	 *  NAME-based, and it is a CONJUNCTION of two lists the code already carries:
	 *
	 *    * `ImplementsNamedStage(Stage, FStages)` -- is this stage part of the SEQUENCE this dispatch
	 *      drives? A stage outside the sequence answers false BY CONSTRUCTION, and the bridge's
	 *      cross-batch rule depends on exactly that (a declaration whose target stage belongs to
	 *      another batch of the same frame must not be judged here -- see FFrameBridge::Build).
	 *    * `Frame.ImplementsStage(Stage)` -- does the FRAME mount it? `MAHO_DECLARE_FRAME` answers
	 *      from the frame's own `IPipeline<TStages...>` list.
	 *
	 *  Both halves used to be RTTI: a `typeid` comparison against this sequence's types, then a
	 *  `dynamic_cast` to the matched interface. Neither is needed now -- and the interface set a
	 *  frame declares IS the set it implements, so the two can no longer disagree. */
	[[nodiscard]] bool Implements(const FFrameExtension& Frame, std::string_view Stage) const override
	{
		return Detail::ImplementsNamedStage(Stage, FStages{}) && Frame.ImplementsStage(Stage);
	}

	/** The stage's body: a DIRECT VIRTUAL CALL through the frame's own subobject for that stage (D8).
	 *  `Round` is the frame number the bridge is building this batch FOR: it becomes the node's own
	 *  immutable round, handed to the stage when the node RUNS -- however late that is relative to the
	 *  slot's reuse (see FFrameCall). */
	[[nodiscard]] std::function<void()> MakeClosure(FFrameExtension& Frame, std::string_view Stage,
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
	std::function<void()> MakeClosureImpl(FFrameExtension& Frame, std::string_view Stage,
		std::uint64_t Round, TTypeList<TCurrent, TRest...>)
	{
		if (Stage == TCurrent::StaticName())
		{
			// THE COMPILE-TIME CHECK the round's declaration used to give (TStageSignature): the stage
			// says which collector drives it (its own `Context`), and this sequence says which collector
			// it is driving. A mismatch means a model error (a frame installed into a collector whose
			// sequence it does not belong to), and it is a MODEL error even when the conversion would
			// compile -- so it is checked exactly, not left to the implicit derived-to-base conversion.
			static_assert(std::is_same_v<typename TCurrent::TContextType, TContext>,
				"this stage's declared CONTEXT is not the collector that drives the sequence it was "
				"selected into: the stage's MAHO_DECLARE_STAGE and the stage sequence disagree about who "
				"owns the call");

			// ... and the SLOT follows from the same fact (both are `TCollectorContext<Collector>`), so a
			// stage declared for ANOTHER collector cannot be selected into this sequence either. This is
			// what the erased `void*` slot used to leave unchecked: it compiled for any target type and
			// only failed at runtime, inside the stage body.
			static_assert(std::is_same_v<typename TCurrent::TSlotType, FSlotType>,
				"this stage's SLOT CONTEXT is not the driving collector's: a stage declares its slot "
				"through TCollectorContext<its own Context>, so this means the stage was declared for a "
				"different collector than the sequence it is being selected into");

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
			FSlotType* TargetSlot = &FrameSlot;
			return [TargetFrame, TargetContext, TargetSlot, Round]()
			{
				// The ambient call context (the slot + THIS node's round) is published for the duration
				// of the call, exactly as before -- a stage body's helper reads it (GetCurrentFrameContext
				// / GetCurrentStageFrame) instead of being handed it.
				FScopedFrameContext Ambient(FFrameCall{ TargetSlot, Round });

				// The frame's OWN subobject for this stage -- the whole of what the old (frame, stage)
				// binding table used to spell. Null cannot happen here (the bridge asked `Implements`
				// before emitting the node), but the check costs nothing and documents the contract.
				if (auto* Instance = static_cast<TCurrent*>(TargetFrame->GetStageInstance(TCurrent::StaticName())))
				{
					TCurrent::InvokeStage(*Instance, *TargetContext, *TargetSlot);
				}
			};
		}
		if constexpr (sizeof...(TRest) > 0)
		{
			return MakeClosureImpl(Frame, Stage, Round, TTypeList<TRest...>{});
		}
		return {};
	}

	template <typename... TRest>
	std::function<void()> MakeClosureImpl(FFrameExtension&, std::string_view, std::uint64_t, TTypeList<>)
	{
		return {};
	}

	TContext& Context;

	/** The SLOT's context (`Frame % K`), NOT the round -- see FFrameCall. A REFERENCE, and every
	 *  closure captures its ADDRESS rather than a copy: a context carries per-frame state (a pass list
	 *  guarded by a mutex, in the render case) and is deliberately not copyable, while the reference
	 *  itself is stable for the collector's whole life (the ring is allocated once, at its full
	 *  depth, and never moved). */
	FSlotType& FrameSlot;
};

} // namespace Maho
