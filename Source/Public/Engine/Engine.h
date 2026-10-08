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

public:
	/** ── THE PIPELINE DEPTH K: the host's vocabulary for Core's process-wide value ───────────────
	 *
	 *  K (frames in flight == the phase-ring width == the number of copies every frame-domain ring
	 *  must hold) is a RUN-TIME property of the process. Its single authority is Core
	 *  (`SetFramesInFlight` / `GetFramesInFlight`, Core/FrameGraph.h): the host writes it ONCE, before
	 *  the first graph exists, and the first `FFrameGraph` construction SEALS it. This pair exists so
	 *  a layer that is handed an `FEngineBase&` can ask the HOST for K instead of reading a
	 *  compile-time bound -- `MAHO_FRAMES_IN_FLIGHT` is only the default a host that injects nothing
	 *  gets, never a size.
	 *
	 *  The render layer is the one that must be told: its device rings, the pool's frame-slot buckets
	 *  and the frame-domain resource lifetimes are all K-deep, and they are sized from the value this
	 *  returns. Nothing clamps: a mismatch between two layers that both claim to hold K copies is
	 *  reported (see FRender::Initialize) instead of being silently hidden.
	 *
	 *  `SetFramesInFlight` here is a forward to Core's, so the host's override point and the engine
	 *  core share ONE value (there is no second copy to drift). */
	[[nodiscard]] std::int32_t GetFramesInFlight() const noexcept;
	void SetFramesInFlight(std::int32_t InFramesInFlight);

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
	/** One context per ring slot, allocated ONCE at the INJECTED pipeline depth (`GetFramesInFlight()`)
	 *  on first use -- never sized by a compile-time bound: the depth is a run-time property of the
	 *  process (the host injects it before the first graph exists). A fixed-size array behind a
	 *  `unique_ptr`, not a growable container: a context is not movable (it holds a mutex), so growing
	 *  would have to move slots that stages already hold references to. */
	std::unique_ptr<FContext[]> Slots;

	/** The base's only way to reach a slot's context, type-erased on purpose (see FFrameBuilder).
	 *  Must never return nullptr: every stage takes the context by reference -- so the whole ring is
	 *  created here, at its full depth, on the very first call. The phases the dispatcher asks for are
	 *  `PhaseOfFrame(round)` in [0, K), i.e. exactly the array's size. */
	void* GetContext(int Slot) override
	{
		if (!Slots)
		{
			Slots = std::make_unique<FContext[]>(static_cast<std::size_t>(GetFramesInFlight()));
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
