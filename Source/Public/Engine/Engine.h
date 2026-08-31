#pragma once

#include <Core/Assembly.h>
#include <Core/Interface.h>
#include <Engine/Query.h>
#include <Engine/Layer.h>
#include <Engine/LayerCollector.h>
#include <Engine/LayerTaskGraph.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#define MAHO_DECLARE_ENGINE(EngineType, DLL)            \
public:                                                 \
	static Maho::FEngineBase* CreateEngine()            \
	{                                                   \
		return new EngineType();                        \
	}                                                   \
	static std::string_view GetModulePath()             \
	{                                                   \
		return DLL;                                     \
	}                                                   \

/**
 * Stage dispatch specializations - the primary template + specialization sugar
 * are declared in Layer.h. Here each engine stage interface gets a full
 * specialization for the FEngineBase context.
 */

namespace Maho
{
class FEngineBase;

class MAHO_API IPreInit
{
public:
	virtual ~IPreInit() = default;
	virtual void PreInitialize(FEngineBase&) = 0;
};

class MAHO_API IInit
{
public:
	virtual ~IInit() = default;
	virtual void Initialize(FEngineBase&) = 0;
};

class MAHO_API IPostInit
{
public:
	virtual ~IPostInit() = default;
	virtual void PostInitialize(FEngineBase&) = 0;
};

class MAHO_API IPreShutdown
{
public:
	virtual ~IPreShutdown() = default;
	virtual void PreShutdown(FEngineBase&) = 0;
};

class MAHO_API IShutdown
{
public:
	virtual ~IShutdown() = default;
	virtual void Shutdown(FEngineBase&) = 0;
};

class MAHO_API IPostShutdown
{
public:
	virtual ~IPostShutdown() = default;
	virtual void PostShutdown(FEngineBase&) = 0;
};

/** Begin-frame capability. */
class MAHO_API IBeginFrame
{
public:
	virtual ~IBeginFrame() = default;
	virtual void BeginFrame(FEngineBase&) = 0;
};

/** Tick capability. */
class MAHO_API ITick
{
public:
	virtual ~ITick() = default;
	virtual void Tick(FEngineBase&) = 0;
};

/** End-frame capability. */
class MAHO_API IEndFrame
{
public:
	virtual ~IEndFrame() = default;
	virtual void EndFrame(FEngineBase&) = 0;
};

/** Exit capability -- request a running loop (IMain) to stop. */
class MAHO_API IExit
{
public:
	virtual ~IExit() = default;
	virtual void RequestExit(FEngineBase&) = 0;
};

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

// Engine base class
class MAHO_API FEngineBase : public FLayerCollector<FEngineBase>
{
public:
	FEngineBase();
	virtual ~FEngineBase();

	virtual void ParseCommandLine(int Argc, char** Argv);

	virtual void PreMain() = 0;

	virtual void PostMain() = 0;

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

private:
	std::atomic<bool> bIsShuttingDown = false;   // whether the engine is shutting down

private:
	// command lines parsing
	std::map<std::string, std::string> Store;
};

} // namespace Maho
