#pragma once

#include <Core/Assembly.h>
#include <Core/Interface.h>
#include <Core/Query.h>
#include <Engine/Layer.h>

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

#define MAHO_DECLARE_ENGINE_LAYER(FeatureType, DLL)     \
public:                                                 \
	MAHO_DECLARE_LAYER(FeatureType)				\
	static Maho::FLayerBase* CreateLayer()            \
	{                                                   \
		return new FeatureType();                       \
	}                                                   \
	static std::string_view GetModulePath()             \
	{                                                   \
		return DLL;                                     \
	}                                                   \

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

/**
 * Stage dispatch specializations - the primary template is declared in
 * Layer.h. Each stage interface gets a full specialization here that
 * dynamic_casts the layer to the interface and calls its stage method. A layer
 * that does not implement the interface silently skips.
 */
#define MAHO_DECLARE_STAGE_DISPATCH(StageType, CastType, Method)  \
template <>                                                       \
inline void Invoke<StageType>(FLayerBase* Layer, FEngineBase& Engine) \
{                                                                 \
	if (auto* S = dynamic_cast<CastType*>(Layer))                 \
	{                                                             \
		S->Method(Engine);                                        \
	}                                                             \
}

MAHO_DECLARE_STAGE_DISPATCH(IPreInit,     IPreInit,     PreInitialize)
MAHO_DECLARE_STAGE_DISPATCH(IInit,        IInit,        Initialize)
MAHO_DECLARE_STAGE_DISPATCH(IPostInit,    IPostInit,    PostInitialize)
MAHO_DECLARE_STAGE_DISPATCH(IPreShutdown, IPreShutdown, PreShutdown)
MAHO_DECLARE_STAGE_DISPATCH(IShutdown,    IShutdown,    Shutdown)
MAHO_DECLARE_STAGE_DISPATCH(IPostShutdown,IPostShutdown,PostShutdown)
MAHO_DECLARE_STAGE_DISPATCH(IBeginFrame,  IBeginFrame,  BeginFrame)
MAHO_DECLARE_STAGE_DISPATCH(ITick,        ITick,        Tick)
MAHO_DECLARE_STAGE_DISPATCH(IEndFrame,    IEndFrame,    EndFrame)
MAHO_DECLARE_STAGE_DISPATCH(IExit,        IExit,        RequestExit)

#undef MAHO_DECLARE_STAGE_DISPATCH

// Engine base class
class MAHO_API FEngineBase : public FQuery<FLayerBase>
{
public:
	FEngineBase();
	virtual ~FEngineBase();

	virtual void ParseCommandLine(int Argc, char** Argv);

	virtual void PreMain() = 0;

	virtual void PostMain() = 0;

	int Main();
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
public:
	/** Get the active feature instances (read-only, for subclass/external queries). */
	const std::vector<std::unique_ptr<FLayerBase>>& GetLayers() const
	{
		return Features;
	}

	/** Request the main loop to exit at the next frame boundary. */
	void RequestExit();

	/** Install sugar: take ownership of a pipeline instance (takes effect next frame). */
	void Install(FLayerBase* Pipeline);

	/**
	 * Dynamically load a feature DLL via FAssembly and install it (takes effect
	 * next frame). The engine holds a unique_ptr for FAssembly + FEngineLayer
	 * (Modules/Features); on unload the engine deletes the instance and calls
	 * FreeLibrary itself.
	 */
	void Install(std::string_view DllPath, const char* FactorySymbol = "CreateLayer");

	/**
	 * Random uninstall request -- unconditionally recorded into the pending
	 * uninstall set (no immediate validation).
	 *
	 * The safe point FlushUnload first applies PendingAdded, then builds a
	 * min-heap from all pending remove requests and greedily unloads: within the
	 * same frame A may be blocked by B, but if B is also in the uninstall
	 * request, B pops first and after B is unloaded A's dependency count drops to
	 * zero, so A pops next -- chained unload within one request.
	 */
	void RequestUninstall(FLayerBase* Pipeline);

	/**
	 * Anonymous uninstall request -- unload addressed by layer name (GetName()).
	 * Equivalent to RequestUninstall, just by name instead of pointer. If no
	 * active layer with the same name exists, it is ignored.
	 */
	void TryUninstall(std::string_view LayerName);

protected:
	// -- FQuery data source --
	std::vector<FLayerBase*>& GetQueryData() override { return Pipelines; }
	const std::vector<FLayerBase*>& GetQueryData() const override { return Pipelines; }

protected:
	/** Apply pending installs/uninstalls (called at the main loop safe point). */
	void FlushPendingUpdatePipelines();

	/** The engine owns feature instances + DLLs; on unload it deletes + FreeLibrary them together. */
	void DeleteUnloaded(FLayerBase* Layer);

private:
	/** Rebuild the reverse dependency count: layer name -> depended-on count (full recompute over active layers). */
	void RebuildReverseDeps();

	/**
	 * Min-heap greedy unload -- build a heap from all pending remove requests
	 * (ordered by depended-on count). Each step unloads one layer whose
	 * depended-on count is 0, updates the counters of its dependents and pushes
	 * them back into the heap, until the heap top has depended-on count > 0 (the
	 * rest are still depended on, so they are abandoned). Dependent requests in
	 * the same batch complete in a chain.
	 */
	void FlushUnload();

private:
	std::vector<FLayerBase*> Pipelines;          // currently active pipelines (anonymous, the graph only knows FLayerBase)
	std::vector<FLayerBase*> PendingAdded;     // pending installs

	std::set<FLayerBase*>    PendingRemoveRequests;  // random uninstall request set
	std::map<std::string, int> ReverseDepCount;        // layer name -> depended-on count

	std::vector<std::unique_ptr<FAssembly>>     Modules;   // DLL keep-alive (move-only)
	std::vector<std::unique_ptr<FLayerBase>>  Features;  // feature instance ownership

	std::atomic<bool> bIsShuttingDown = false;   // whether the engine is shutting down

	FThreadPool Pool;

private:
	// command lines parsing
	std::map<std::string, std::string> Store;
};

} // namespace Maho
