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
}

FGameWorld::~FGameWorld()
{
	// WorldGraph is reset in Shutdown; ComponentPools cleared there too.
}

template <typename... TStages>
bool FGameWorld::ExecuteGraph()
{
	WorldGraph->Init(Select<TStages...>());
	if (!WorldGraph->Compile())
	{
		ReportError("FGameWorld::ExecuteGraph: world stage graph Compile failed");
		return false;
	}
	WorldGraph->Execute();
	return true;
}

// -- engine stage overrides (host drives these). Initialize/Tick/Shutdown carry
// the real work; the remaining stages are empty -- FGameWorld only hosts the world
// and schedules its systems, the per-stage ECS frame runs in Tick.
void FGameWorld::PreInitialize(FEngineBase&) {}
void FGameWorld::PostInitialize(FEngineBase&) 
{
	WorldGraph = std::make_unique<FLayerTaskGraph<FWorldStages, FGameWorld>>(Pool, *this);
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
	// FlushPendingUpdatePipelines safe point (IOnInstalled). UISystem owns the
	// game->render UI broker (+ the draggable text-box placeholder); it submits its
	// draw closure via the UIBuilder, which FUIFeature runs on the render worker.
	Install<GameWorld::FUISystem>();
}

void FGameWorld::Tick(FEngineBase&)
{
	if (!WorldGraph)
	{
		return;
	}

	// Frame-start barrier: wait the PREVIOUS frame's trailing stage group. The
	// post-fixed group (IUpdate/ILateUpdate) is dispatched un-flushed below, so it
	// pipelines across frames into this wait -- cross-frame parallelism, same as
	// the render graph. Must precede Init (a rebuild under live tasks is a use of
	// a freed node).
	WorldGraph->Flush();

	// Apply pending install/uninstall of world systems (attach -> IOnInstalled,
	// detach -> IPreUnInstall) at the safe point.
	FlushPendingUpdatePipelines<TTypeList<IOnInstalled>, TTypeList<IPreUnInstall>>();

	// Delta time.
	const auto Now = std::chrono::steady_clock::now();
	DeltaSeconds = std::chrono::duration<float>(Now - LastFrame).count();
	LastFrame = Now;

	// Pre-fixed: resolve input once, synchronously -- simulation must read a
	// settled input state, so flush immediately after dispatch.
	ExecuteGraph<IProcessInput>();
	WorldGraph->Flush();

	// Fixed timestep: 0..N steps this frame, each strictly ordered (a step cannot
	// overlap the next, so each is flushed before the next builds).
	Accumulator += DeltaSeconds;
	while (Accumulator >= FixedStepSeconds)
	{
		if (!ExecuteGraph<IFixedUpdate>())
		{
			break;
		}
		WorldGraph->Flush();
		Accumulator -= FixedStepSeconds;
	}

	// Post-fixed: update + late update, dispatched WITHOUT a trailing flush so they
	// pipeline across frames; the next Tick's leading Flush (above) waits them.
	ExecuteGraph<IUpdate, ILateUpdate>();
}

void FGameWorld::Shutdown(FEngineBase&)
{
	// Uninstall every world system through the collector teardown pipeline so each
	// system's IPreUnInstall runs BEFORE its instance is destroyed. A bare member
	// destruction (UISystem blowing away with this object) would skip the teardown
	// stage and leak cross-module subscriptions (e.g. a system's std::function bound
	// into another DLL's event, whose target manager lives in the unloaded DLL).
	for (FLayerBase* L : Pipelines)
	{
		TryUninstall(L->GetName());
	}
	FlushPendingUpdatePipelines<TTypeList<IOnInstalled>, TTypeList<IPreUnInstall>>();

	if (WorldGraph)
	{
		WorldGraph->Flush();   // drain any leftover world-system tasks
		WorldGraph.reset();
	}
	ComponentPools.clear();
	GGameWorld = nullptr;
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
