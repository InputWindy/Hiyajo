#pragma once

#include <Core/Assembly.h>
#include <Core/Interface.h>
#include <Engine/Query.h>
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
 * Stage dispatch specializations - the primary template + specialization sugar
 * are declared in Frame.h. Here each engine stage interface gets a full
 * specialization for the FEngineBase context.
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

class MAHO_API IPreInit
{
public:
	virtual ~IPreInit() = default;
	virtual void PreInitialize(FEngineBase&, FEngineContext&) = 0;
};

class MAHO_API IInit
{
public:
	virtual ~IInit() = default;
	virtual void Initialize(FEngineBase&, FEngineContext&) = 0;
};

class MAHO_API IPostInit
{
public:
	virtual ~IPostInit() = default;
	virtual void PostInitialize(FEngineBase&, FEngineContext&) = 0;
};

class MAHO_API IPreShutdown
{
public:
	virtual ~IPreShutdown() = default;
	virtual void PreShutdown(FEngineBase&, FEngineContext&) = 0;
};

class MAHO_API IShutdown
{
public:
	virtual ~IShutdown() = default;
	virtual void Shutdown(FEngineBase&, FEngineContext&) = 0;
};

class MAHO_API IPostShutdown
{
public:
	virtual ~IPostShutdown() = default;
	virtual void PostShutdown(FEngineBase&, FEngineContext&) = 0;
};

/** Begin-frame capability. */
class MAHO_API IBeginFrame
{
public:
	virtual ~IBeginFrame() = default;
	virtual void BeginFrame(FEngineBase&, FEngineContext&) = 0;
};

/** Tick capability. */
class MAHO_API ITick
{
public:
	virtual ~ITick() = default;
	virtual void Tick(FEngineBase&, FEngineContext&) = 0;
};

/** End-frame capability. */
class MAHO_API IEndFrame
{
public:
	virtual ~IEndFrame() = default;
	virtual void EndFrame(FEngineBase&, FEngineContext&) = 0;
};

/** Exit capability -- request a running loop (IMain) to stop. */
class MAHO_API IExit
{
public:
	virtual ~IExit() = default;
	virtual void RequestExit(FEngineBase&, FEngineContext&) = 0;
};

// The per-stage dispatch specializations live AT THE BOTTOM of this header, after `class FEngineBase`:
// their body names `FEngineBase::FContext`, which is only declared inside the class. (Declaring them
// here would fail: the class is still incomplete, and unlike a template body a full specialization's
// body is compiled right where it is written.)

using FInitStages = TTypeList<IPreInit, IInit, IPostInit>;
using FTickStages = TTypeList<IBeginFrame, ITick, IEndFrame, IExit>;
using FShutdownStages = TTypeList<IPreShutdown, IShutdown, IPostShutdown>;

// Engine base class
//
// ONE base: FFrameBuilder<FEngineBase> is the collector + frame loop. The graph this host drives is
// no longer a base class either -- it is a MEMBER (see Graph below), created on first use at this
// host's own pool + default lane and injected into the builder (`SetGraph`). The host is the only
// frame whose graph has no owner name: its frames ARE the top level.
class MAHO_API FEngineBase : public FFrameBuilder<FEngineBase>
{
public:
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

	/** Create + start the graph on FIRST use, at this host's own pool + default lane, and hand it to
	 *  the batch builder (`SetGraph`) -- which is the only party that turns declarations into
	 *  submissions. Idempotent, and cheap enough to call from every driving entry point. */
	FFrameGraph& EnsureGraph()
	{
		if (!bGraphReady)
		{
			bGraphReady = true;
			Graph.emplace(GetPool(), GetPoolLane());
			Graph->Initialize();
			SetGraph(*Graph);
		}
		return *Graph;
	}

public:
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

	/** The base's only way to reach a slot's context, type-erased on purpose (see FFrameBuilder).
	 *  Must never return nullptr: every stage takes the context by reference -- so the whole ring is
	 *  created here, at its full depth, on the very first call. The phases the dispatcher asks for are
	 *  `PhaseOfFrame(round)` in [0, K), i.e. exactly the array's size. */
	void* GetContext(int Slot) override
	{
		if (!Slots)
		{
			// Qualified: the engine's Base used to carry a `GetFramesInFlight()` of its own (a forward
			// to Core's). What remains is Core's, and naming it explicitly keeps the call unambiguous
			// if this class ever grows a member of that name again.
			Slots = std::make_unique<FContext[]>(static_cast<std::size_t>(::Maho::GetFramesInFlight()));
		}
		return &Slots[static_cast<std::size_t>(Slot)];
	}
};

// Stage dispatch for the engine's own stages. Placed AFTER the class on purpose: each macro expansion
// names `FEngineBase::FContext`, and a full specialization's body is compiled where it is written --
// so the class must be complete first (a template body could defer that; this cannot).

MAHO_DECLARE_STAGE_DISPATCH(FEngineBase, IPreInit,     IPreInit,     PreInitialize)
MAHO_DECLARE_STAGE_DISPATCH(FEngineBase, IInit,        IInit,        Initialize)
MAHO_DECLARE_STAGE_DISPATCH(FEngineBase, IPostInit,    IPostInit,    PostInitialize)
MAHO_DECLARE_STAGE_DISPATCH(FEngineBase, IPreShutdown, IPreShutdown, PreShutdown)
MAHO_DECLARE_STAGE_DISPATCH(FEngineBase, IShutdown,    IShutdown,    Shutdown)
MAHO_DECLARE_STAGE_DISPATCH(FEngineBase, IPostShutdown,IPostShutdown,PostShutdown)
MAHO_DECLARE_STAGE_DISPATCH(FEngineBase, IBeginFrame,  IBeginFrame,  BeginFrame)
MAHO_DECLARE_STAGE_DISPATCH(FEngineBase, ITick,        ITick,        Tick)
MAHO_DECLARE_STAGE_DISPATCH(FEngineBase, IEndFrame,    IEndFrame,    EndFrame)
MAHO_DECLARE_STAGE_DISPATCH(FEngineBase, IExit,        IExit,        RequestExit)

} // namespace Maho
