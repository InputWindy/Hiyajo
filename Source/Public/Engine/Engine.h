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

protected:
	/** One context per ring slot. The DERIVED class owns the storage because the base cannot name
	 *  the nested type -- `FEngineBase::FContext` is incomplete while the base is instantiated. */
	std::array<FContext, MAHO_FRAMES_IN_FLIGHT> Slots;

	/** The base's only way to reach a slot's context, type-erased on purpose (see FFrameBuilder).
	 *  Must never return nullptr: every stage takes the context by reference. */
	void* GetContext(int Slot) override
	{
		return &Slots[Slot];
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
