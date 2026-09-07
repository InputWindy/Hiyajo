#include "GameWorld.h"

// UISystem is a peer WORLD SYSTEM installed here (sub-landlord). Include only in
// the private cpp so GameWorld.h stays free of world-system types.
#include <UISystem.h>

// Resource texture enumeration for the default UI widget (FUIWidget::Image controls).
#include <Resource.h>
#include <AssetTypes.h>

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
	MyStage<IInit>().IsWaiting<Resource::FResourceSystem>().ForStage<IInit>();

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

	// Startup test texture: SYNCHRONOUS (blocking) import on THIS thread so the resource
	// is resident + mirrored BEFORE the UI texture browser rebuilds its Image controls
	// (see FUISystem::Update). Async Import would poll on Tick, which does not run during
	// a stage -- the browser would see an empty set on the first frame. ImportBlocking
	// reads / decodes / registers / broadcasts here, so OnAssetImported fires the render
	// mirror upload immediately.
	if (Resource::FResourceSystem* RS = Resource::GetResourceSystem())
	{
		RS->ImportBlocking<Resource::FTexture2D>({ "D:/TestPackage/test.png" });
	}
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

	// Default UI entity: bind the ECS UI component (FUIWidget) to a default entity and
	// enumerate every texture the FResourceSystem has loaded into FUIControl::Image
	// controls. UISystem's Update renders ALL FUIWidget entities (see
	// GetAllWithComponent), so this widget is drawn with ImGui::Image exactly like any
	// other -- the world defines WHAT (data), the UI system defines HOW (ImGui).
	DefaultUIEntity = CreateEntity();
	AddComponent<FTransform>(DefaultUIEntity, FTransform{ 60.f, 60.f, 0.f });

	FUIWidget W;
	W.X = 60.f;
	W.Y = 60.f;
	W.Width = 520.f;
	W.Height = 440.f;
	W.bVisible = true;
	// Live texture browser: texture imports are asynchronous, so a one-time
	// enumeration here (IInit) would see an empty set and no thumbnails. Instead the
	// UISystem rebuilds this widget's Image controls from the CURRENT resource texture
	// set every frame (see FUISystem::Update / bPreviewAllTextures), so async imports
	// appear without re-running this Initialize.
	W.bPreviewAllTextures = true;
	W.Controls.push_back(FUIControl{ EUIControlType::Label, "default texture browser" });
	W.Controls.push_back(FUIControl{ EUIControlType::Separator });
	AddComponent<FUIWidget>(DefaultUIEntity, W);

	// Install the world systems (peer layers). Applied at the next Tick's
	// FlushPendingUpdatePipelines safe point (IOnInstalled).
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
	if (DefaultUIEntity.IsValid())
	{
		DestroyEntity(DefaultUIEntity);
		DefaultUIEntity = {};
	}
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
