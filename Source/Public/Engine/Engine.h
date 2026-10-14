#pragma once

#include <Core/Assembly.h>
#include <Core/Interface.h>
#include <Core/Query.h>
#include <Engine/DerivedEdges.h>
#include <Engine/Frame.h>
#include <Engine/FrameBuilder.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#define MAHO_DECLARE_ENGINE(EngineType)                     \
public:                                                      \
	static Maho::FEngineBase* CreateEngine()                 \
	{                                                        \
		return new EngineType();                              \
	}                                                        \
	static std::string GetModulePath()                       \
	{                                                        \
		return Maho::ApplyModuleExtension(#EngineType);        \
	}                                                        \
	static constexpr std::string_view StaticName()           \
	{                                                        \
		return #EngineType;                                    \
	}                                                        \
	std::string_view GetName() const                \
	{                                                        \
		return StaticName();                                  \
	}

/**
 * Stage declarations - the declaring macro and the dispatch policy are declared in Frame.h. Here each
 * engine stage interface gets its declaration (identity + typed entry) for the ENGINE's batches.
 */

namespace Maho
{
class FEngineBase;

/** Per-frame state for the ENGINE's own stages (one instance per ring slot; reach it as
 *  `FEngineBase::FContext`).
 *
 *  DEFINED HERE, at namespace scope, and not inside the class: the stage interfaces below must name
 *  it in their signatures, and they are declared before `FEngineBase` exists. `FEngineBase` carries
 *  a nested alias so stages and the dispatch macro can still write the nested name. */
struct FEngineContext
{
};

/** The engine host's per-frame context, mapped from the collector's NAME: the stage declarations below
 *  must spell the slot type while `FEngineBase` is still incomplete (they are written before it, and it
 *  inherits them). Declared here, next to the context itself, so the type and its mapping are one fact
 *  in one place -- a specialization needs only the forward declaration above. */
template <>
struct TCollectorContext<FEngineBase>
{
	using Type = FEngineContext;
};

// The engine's 10 stage interfaces. ONE LINE EACH: the macro IS the class (its identity, its typed
// entry, and the export tag the consumer has to import). Nothing else is declared for a stage -- its
// protocol is the declaration, so it registers no metadata row of its own.

MAHO_DECLARE_STAGE(MAHO_API, IPreInit, PreInitialize, FEngineBase)
MAHO_DECLARE_STAGE(MAHO_API, IInit, Initialize, FEngineBase)
MAHO_DECLARE_STAGE(MAHO_API, IPostInit, PostInitialize, FEngineBase)
MAHO_DECLARE_STAGE(MAHO_API, IPreShutdown, PreShutdown, FEngineBase)
MAHO_DECLARE_STAGE(MAHO_API, IShutdown, Shutdown, FEngineBase)
MAHO_DECLARE_STAGE(MAHO_API, IPostShutdown, PostShutdown, FEngineBase)

/** Begin-frame capability. */
MAHO_DECLARE_STAGE(MAHO_API, IBeginFrame, BeginFrame, FEngineBase)
/** Tick capability. */
MAHO_DECLARE_STAGE(MAHO_API, ITick, Tick, FEngineBase)
/** End-frame capability. */
MAHO_DECLARE_STAGE(MAHO_API, IEndFrame, EndFrame, FEngineBase)
/** Exit capability -- request a running loop (IMain) to stop. */
MAHO_DECLARE_STAGE(MAHO_API, IExit, RequestExit, FEngineBase)

// The engine's stages declare NO metadata row of their own (see the note at the bottom of this
// header): a row belongs to the implementing frame, so nothing here has to wait for `class
// FEngineBase` to be complete.

using FInitStages = TTypeList<IPreInit, IInit, IPostInit>;
using FTickStages = TTypeList<IBeginFrame, ITick, IEndFrame, IExit>;
using FShutdownStages = TTypeList<IPreShutdown, IShutdown, IPostShutdown>;

// Engine base class
//
// ONE base: FFrameBuilder<FEngineBase> is the collector + frame loop. The graph this host drives is
// no longer a base class either -- it is a MEMBER (see Graph below), created on first use at this
// host's own pool + default lane and passed INTO every driving call by the call site. The host is the
// only frame whose graph has no owner name: its frames ARE the top level.
class MAHO_API FEngineBase : public FFrameBuilder<FEngineBase>
{
public:
	/** The COLLECTOR's own name. Every other collector gets one from `MAHO_DECLARE_FRAME`
	 *  (`FRender`, `FGameWorld`, `FExampleEditor`); the host BASE has no such declaration -- it is the
	 *  collector every plain `MAHO_DECLARE_FRAME` sub-frame names, and its own identity is the one the
	 *  install-time check compares against (see `FFrameBuilder::CollectorName`, task 2'4).
	 *
	 *  DISTINCT from the HOST's name on purpose: `MAHO_DECLARE_ENGINE` gives the concrete host
	 *  (`FExampleEngine`) its own `StaticName()`, which is the INSTALL TREE's root key, while this is
	 *  the name of the collector ROLE the host plays -- a host sub-frame declares `FEngineBase`, not the
	 *  engine it happens to be running under. */
	static constexpr std::string_view StaticName() noexcept
	{
		return "FEngineBase";
	}

	/** Per-frame state for the ENGINE's own stages -- one instance per ring slot.
	 *
	 *  EMPTY FOR NOW on purpose: this step only establishes the slot plumbing (a stage receives the
	 *  context of the frame it belongs to, instead of the engine object every frame shares).
	 *  Moving "per-frame" members into it is the next step, and it is where the real work is.
	 *
	 *  An ALIAS, not the definition: the stage interfaces must name this type before FEngineBase
	 *  exists, so the definition lives at namespace scope (see FEngineContext). Stages and the
	 *  dispatch macro write the nested name; it is the same type either way. */
	using FContext = FEngineContext;

	FEngineBase();
	virtual ~FEngineBase();

	virtual void ParseCommandLine(int Argc, char** Argv);

	virtual void PreMain() = 0;

	virtual void PostMain();

	virtual int Main();

	/** The frame NUMBER (the round) of the iteration about to be driven -- read EXACTLY ONCE per
	 *  frame by `Main`, and then used for that whole frame (every sequence's tick batch, and
	 *  whatever else the frame tags itself with). One read per frame is the point: two reads could
	 *  straddle a frame boundary and put two halves of one frame into different rounds.
	 *
	 *  The CANONICAL number belongs to a plugin -- `Platform::GetFrameIndex()`, whose single writer
	 *  is `FPlatform::IBeginFrame` -- and this layer must not include a plugin header, so the HOST
	 *  supplies it: the project entry point overrides this (see `FExampleEngine`).
	 *
	 *  DEFAULT: a monotonic counter owned by this loop. It exists so a host that HAS no frame source
	 *  (the test projects) still drives the ring in order instead of pushing every frame into one
	 *  slot; a host that has a source overrides it, and then this counter is dead. */
	[[nodiscard]] virtual std::uint64_t NextFrameNumber();

protected:
	/** ── THE GRAPH THIS HOST DRIVES ───────────────────────────────────────────────────────────────
	 *
	 *  The host OWNS the graph its tick sequence is scheduled on -- it is a MEMBER, not a base class
	 *  and not a hidden `unique_ptr` inside the builder (`Engine/FrameBuilder.h` states the rule:
	 *  "a frame that DRIVES a stage sequence owns the FFrameGraph that sequence is scheduled on").
	 *  Owning it here is what makes the drain visible at this layer: `Main` fences it before the pool
	 *  is torn down, and the member's own destructor stops the scheduler thread while the pool it was
	 *  built with is still alive (members die before base classes -- `FFrameBuilder` holds the pool).
	 *
	 *  IT IS CREATED ON FIRST USE (`EnsureGraph`), never in the constructor, and for one reason: K
	 *  (the pipeline depth) is a RUN-TIME value whose authority is a plugin-side CVar
	 *  (`r.FramesInFlight`, read through Core's `GetFramesInFlight`), and a graph resolves it once, on
	 *  first use. A graph built at construction time would be built before the plugins exist.
	 *
	 *  `std::optional` because `FFrameGraph` is neither copyable nor movable (it is a
	 *  `FThreadedServer` with a resident thread): the storage has to be constructed in place, once. */
	std::optional<FFrameGraph> Graph;

	/** True once `EnsureGraph` has created + started the graph. The two driving entry points read it
	 *  instead of `Graph.has_value()` so the "was it ever used" question has one name. */
	bool bGraphReady = false;

	/** Create + start the graph on FIRST use, at this host's own pool + default lane. Idempotent, and
	 *  cheap enough to call from every driving entry point. */
	FFrameGraph& EnsureGraph()
	{
		if (!bGraphReady)
		{
			bGraphReady = true;
			Graph.emplace(GetPool(), GetPoolLane());
			// The graph derives its edges from RESOURCE ACCESS (Engine/DerivedEdges.h): a stage's
			// declared accesses plus each resource's ordering trait replace the hand-written pairs.
			Graph->SetDeriver(&GetResourceEdgeDeriver());
			Graph->Initialize();
		}
		return *Graph;
	}

public:
	/** THE HOST's graph, for anyone holding nothing but an `FEngineBase&`. Same object `EnsureGraph`
	 *  returns: calling it IS the "create it on first use" step, so a caller never has to ask whether
	 *  the graph exists yet. It is the graph THIS host drives -- a collector frame that drives its own
	 *  sub-graph passes the graph IT owns instead (see the note on `Graph`: two drivers sharing one
	 *  graph would make `Submit`'s per-slot admission wait on the very node that is submitting). */
	FFrameGraph& GetGraph() { return EnsureGraph(); }

	/** THE HOST's pool -- the one thread set the whole engine runs on: every collector lane lives on
	 *  it, and every resident role (`FThreadedServer`, i.e. a module that owns a thread for an OS
	 *  handle) asks for a PERSISTENT lane of it: `Server.UseSharedPool(Engine.GetPool())` right before
	 *  that role starts. Exposed here because the pool belongs to the HOST (the outermost builder owns
	 *  it -- see `FFrameBuilder::OwnedPool`) and a frame only ever holds an `FEngineBase&`.
	 *  Lifetime is safe by construction: Features (the frames) die before the builder's pool does. */
	[[nodiscard]] FThreadPool& GetPool() { return FFrameBuilder<FEngineBase>::GetPool(); }

	/** True when a flag/key is present (whether or not it carries a value). */
	[[nodiscard]] bool Has(std::string_view Key) const;

	/** Value for a key; empty string when absent. */
	[[nodiscard]] std::string Get(std::string_view Key) const;

	/** Value as bool ("true"/"1"/"yes"/"on" -> true). */
	[[nodiscard]] bool GetBool(std::string_view Key) const;

	/** Value as int; 0 (or fallback) when absent/unparseable. */
	[[nodiscard]] int GetInt(std::string_view Key) const;

	/** All parsed key->value pairs (const ref). */
	[[nodiscard]] const std::map<std::string, std::string>& GetAll() const { return Store; }

	/** Request the main loop to exit at the next frame boundary. */
	void RequestExit();

	/** True once RequestExit was called: the host's own vocabulary for the builder's
	 *  "closing" state (the same flag also refuses Install / Reload there). */
	[[nodiscard]] bool ShouldExit() const noexcept { return IsClosing(); }

private:
	// command lines parsing
	std::map<std::string, std::string> Store;

	/** The fallback frame source of `NextFrameNumber` -- advanced only by a host that does NOT
	 *  override it. See the method's own note. */
	std::uint64_t LoopFrame = 0;

protected:
	/** One context per ring slot, allocated ONCE at the process pipeline depth (`GetFramesInFlight()`)
	 *  on first use -- never sized by a compile-time bound: the depth is a run-time property of the
	 *  process (the CVar `r.FramesInFlight`, resolved by Core before the graph's ring is sized). A
	 *  fixed-size array behind a `unique_ptr`, not a growable container: a context is not movable (it
	 *  holds a mutex), so growing would have to move slots that stages already hold references to. */
	std::unique_ptr<FContext[]> Slots;

	/** The base's only way to reach a slot's context. Typed: the slot type is this collector's own
	 *  (`TCollectorContext<FEngineBase>`, see Engine/Frame.h), so no erasure and no cast. Must never
	 *  return anything but a live slot: every stage takes the context by reference -- so the whole ring
	 *  is created here, at its full depth, on the very first call. The phases the dispatcher asks for are
	 *  `PhaseOfFrame(round)` in [0, K), i.e. exactly the array's size. */
	FContext& GetContext(int Slot) override
	{
		if (!Slots)
		{
			// Qualified: the engine's Base used to carry a `GetFramesInFlight()` of its own (a forward
			// to Core's). What remains is Core's, and naming it explicitly keeps the call unambiguous
			// if this class ever grows a member of that name again.
			Slots = std::make_unique<FContext[]>(static_cast<std::size_t>(::Maho::GetFramesInFlight()));
		}
		return Slots[static_cast<std::size_t>(Slot)];
	}
};

// The engine's stages declare NO metadata row. A row belongs to the frame that IMPLEMENTS the stage
// (`MAHO_IMPLEMENT_STAGE(FPlatform, IInit, ...)` in that frame's own .cpp, which is where WHO touches
// what is known); the stage side of the protocol is the interface declaration above, and a
// `MAHO_IMPLEMENT_STAGE(IInit, IInit)` "row" would only restate it with no frame to attribute it to.

} // namespace Maho
