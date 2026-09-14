#include "GameWorld.h"

// UISystem is a peer WORLD SYSTEM installed here (sub-landlord). Include only in
// the private cpp so GameWorld.h stays free of world-system types.
#include <UISystem.h>

#include <vector>

namespace Maho
{
namespace GameWorld
{

static FGameWorld* GGameWorld = nullptr;

FGameWorld* GetGameWorld()
{
	return GGameWorld;
}

FGameWorld::FGameWorld()
{
	// No forward dependencies: the world's Initialize only creates a sample entity and
	// installs the UISystem peer layer (which owns the UI broker). Any resource/type the
	// world needs is reachable through the systems it installs, so it does not gate on
	// other engine plugins (forward WaitFor on an absent plugin breaks graph Compile).
	//
	// Reverse edge only: the UI view registry is an ENVIRONMENT service for this layer's
	// teardown transition. A world system that owns a view (UISystem) unregisters it in its
	// IPreUnInstall, which Shutdown() drives below -- so the registry's IShutdown must run
	// AFTER mine. Declared here, at the layer that actually drives that teardown: a system
	// is installed into MY sub-graph, so an edge it declares itself would never bind (the
	// sub-graph only contains the systems). Addressed by NAME, not by type: FGameWorld is
	// generic scaffolding and must not build-depend on the optional UI plugin; the graph
	// skips an edge whose target is not installed.
	BlockOn("FUIViewRegistry", std::type_index(typeid(IShutdown)), std::type_index(typeid(IShutdown)));
}

FGameWorld::~FGameWorld()
{
	// WorldGraph is reset in Shutdown; ComponentPools cleared there too.
}

// -- engine stage overrides (host drives these). Initialize/Tick/Shutdown carry
// the real work; the remaining stages are empty -- FGameWorld only hosts the world
// and schedules its systems, the per-stage ECS frame runs in Tick.
void FGameWorld::PreInitialize(FEngineBase&) {}
void FGameWorld::PostInitialize(FEngineBase&) 
{
	InputGraph = std::make_unique<FLayerTaskGraph<FInputStages, FGameWorld>>(Pool, *this);
	FixedGraph = std::make_unique<FLayerTaskGraph<FFixedStages, FGameWorld>>(Pool, *this);
	PostGraph = std::make_unique<FLayerTaskGraph<FPostStages, FGameWorld>>(Pool, *this);
	bGraphsDirty = true;
}

void FGameWorld::RebuildGraphs()
{
	if (!InputGraph || !FixedGraph || !PostGraph)
	{
		return;
	}
	TraceTeardown("RebuildGraphs: input Init/Compile");
	InputGraph->Init(Select<IProcessInput>());
	if (!InputGraph->Compile())
	{
		ReportError("FGameWorld: input stage graph Compile failed");
	}
	TraceTeardown("RebuildGraphs: fixed Init/Compile");
	FixedGraph->Init(Select<IFixedUpdate>());
	if (!FixedGraph->Compile())
	{
		ReportError("FGameWorld: fixed-step stage graph Compile failed");
	}
	TraceTeardown("RebuildGraphs: post Init/Compile");
	PostGraph->Init(Select<IUpdate, ILateUpdate>());
	if (!PostGraph->Compile())
	{
		ReportError("FGameWorld: post-fixed stage graph Compile failed");
	}
	TraceTeardown("RebuildGraphs: done");
	bGraphsDirty = false;
}
void FGameWorld::BeginFrame(FEngineBase&) {}
void FGameWorld::EndFrame(FEngineBase&) {}
void FGameWorld::RequestExit(FEngineBase&) {}
void FGameWorld::PreShutdown(FEngineBase&) {}
void FGameWorld::PostShutdown(FEngineBase&) {}

void FGameWorld::Initialize(FEngineBase&)
{
	GGameWorld = this;
	LastFrame = std::chrono::steady_clock::now();
	Accumulator = 0.f;

	// Smoke test the object model: create an entity, attach a FTransform, read it
	// back. Systems (peer layers) will do this against GetGameWorld() too.
	FEntity E = CreateEntity();
	FTransform* T = AddComponent<FTransform>(E, FTransform{1.f, 2.f, 3.f});
	if (T != nullptr && T->Z == 3.f)
	{
		// Component attached + read back correctly.
	}

	// Install the world systems (peer layers). Applied at the next Tick's
	// FlushPendingUpdatePipelines safe point (IOnInstalled). UISystem owns the game-side
	// UI: it declares the demo widget's persistent view tree (and the draggable text-box
	// placeholder) and registers the view in the UI view registry, which the render
	// feature translates once per frame.
	Install<GameWorld::FUISystem>();
}

void FGameWorld::Tick(FEngineBase&)
{
	if (!InputGraph)
	{
		return;
	}

	// Frame-start barrier: the post-fixed group is dispatched un-flushed below, so it
	// pipelines across frames into this wait -- cross-frame parallelism, same as the
	// render graph. Must precede any rebuild (a rebuild under live tasks is a use of a
	// freed node).
	PostGraph->Flush();

	// Safe point for world-system install/uninstall (attach -> IOnInstalled, detach ->
	// IPreUnInstall). A changed system set is the ONLY thing that rebuilds the graphs,
	// and it happens right here, with nothing in flight.
	const bool bSetChanged = !PendingAdded.empty() || !PendingRemoveRequests.empty();
	FlushPendingUpdatePipelines<TTypeList<IOnInstalled>, TTypeList<IPreUnInstall>>();
	if (bSetChanged)
	{
		bGraphsDirty = true;
	}
	if (bGraphsDirty)
	{
		RebuildGraphs();
	}

	// Delta time.
	const auto Now = std::chrono::steady_clock::now();
	DeltaSeconds = std::chrono::duration<float>(Now - LastFrame).count();
	LastFrame = Now;

	// Pre-fixed: resolve input once, synchronously -- simulation must read a
	// settled input state, so flush immediately after dispatch.
	TraceTeardown("Tick: input execute");
	InputGraph->Execute();
	TraceTeardown("Tick: input flush");
	InputGraph->Flush();

	// Fixed timestep: 0..N steps this frame, each strictly ordered (a step cannot
	// overlap the next, so each is flushed before the next runs).
	Accumulator += DeltaSeconds;
	while (Accumulator >= FixedStepSeconds)
	{
		FixedGraph->Execute();
		FixedGraph->Flush();
		Accumulator -= FixedStepSeconds;
	}

	// Post-fixed: update + late update, dispatched WITHOUT a trailing flush so they
	// pipeline across frames; the next Tick's leading Flush (above) waits them.
	TraceTeardown("Tick: post execute");
	PostGraph->Execute();
	TraceTeardown("Tick: done");
}

void FGameWorld::Shutdown(FEngineBase&)
{
	// MY OWN state first, the world systems AFTER. The component pools hold objects
	// whose destructors live in the systems' modules, and the graphs' nodes point at
	// their instances -- freeing either of those once a system's DLL is unloaded runs
	// code from an unmapped module (a hard AV, not a leak).
	TraceTeardown("GameWorld::Shutdown: graphs flush + reset");
	if (PostGraph) { PostGraph->Flush(); }
	if (FixedGraph) { FixedGraph->Flush(); }
	if (InputGraph) { InputGraph->Flush(); }
	PostGraph.reset();
	FixedGraph.reset();
	InputGraph.reset();
	TraceTeardown("GameWorld::Shutdown: ComponentPools clear");
	ComponentPools.clear();
	TraceTeardown("GameWorld::Shutdown: own state released");

	// Now the systems: uninstall through the collector teardown pipeline so each one's
	// IPreUnInstall runs BEFORE its instance is destroyed (a bare member destruction
	// would skip that stage and leak cross-module subscriptions -- e.g. a system's
	// std::function bound into another DLL's event).
	for (FLayerBase* L : Pipelines)
	{
		TryUninstall(L->GetName());
	}
	FlushPendingUpdatePipelines<TTypeList<IOnInstalled>, TTypeList<IPreUnInstall>>();
	TraceTeardown("GameWorld::Shutdown: systems unloaded");

	GGameWorld = nullptr;
	TraceTeardown("GameWorld::Shutdown: done");
}

FEntity FGameWorld::CreateEntity()
{
	return Registry.Create();
}

bool FGameWorld::DestroyEntity(FEntity E)
{
	if (!Registry.Destroy(E))
	{
		return false;
	}
	// Drop every component column slot for this entity index.
	for (const auto& Pool : ComponentPools)
	{
		Pool->Remove(E.Index);
	}
	return true;
}

bool FGameWorld::IsAlive(FEntity E) const
{
	return Registry.IsAlive(E);
}

} // namespace GameWorld
} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_GAMEWORLD_API Maho::FLayerBase* CreateLayer()
{
	return Maho::GameWorld::FGameWorld::CreateLayer();
}
