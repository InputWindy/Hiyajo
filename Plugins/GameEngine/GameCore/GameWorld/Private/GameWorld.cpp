#include "GameWorld.h"

// UISystem is a peer WORLD SYSTEM installed here (sub-landlord). Include only in
// the private cpp so GameWorld.h stays free of world-system types.
#include <UISystem.h>
#include <MeshRenderSystem.h>

// The canonical frame number (Platform::GetFrameIndex): the ROUND each of this collector's
// sequences is driven with -- see FGameWorld::Tick. Built-level dependency, declared in the
// .cplugin; only this cpp names the platform, so GameWorld.h stays free of it.
#include <Platform.h>
#include <Trace.h>
#include <typeinfo>
#include <unordered_map>
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
	MyStage<IShutdown>().IsBlocking("FUIViewRegistry").OnStage<IShutdown>();

	// Reverse edge, same reason, different environment service: the SCRIPT BACKEND must be shut down
	// AFTER me. A pool element is an `FScriptComponent` holding a `shared_ptr<FScriptInstance>`, and
	// that instance's destructor runs `~sol::table`, which pushes the table's registry slot back to
	// reference it (`lua_rawgeti`). `FScriptSystem::IShutdown` calls `Language->Shutdown()`, i.e.
	// `lua_close` on exactly that state -- so the two steps are WRITERS on one structure and must be
	// ordered, or the pool teardown calls into a closed VM (observed: AV inside `lua_rawgeti` reached
	// from `ComponentPools.clear()`, with the world's own IShutdown node as the one being dispatched).
	// BOTH shut down in the same batch on the SAME graph, so without this edge their order is whatever
	// the worker pool picks. Addressed by NAME for the same reason as above: FGameWorld is generic
	// scaffolding and does not build-depend on the optional Script plugin; the graph skips the edge
	// when no script layer is installed (and a process with no Lua pools has nothing to order).
	MyStage<IShutdown>().IsBlocking("FScriptSystem").OnStage<IShutdown>();
}

FGameWorld::~FGameWorld()
{
	// Nothing to release: `Graph` is a member and stops its own scheduler thread in ~FFrameGraph,
	// the pools are cleared in Shutdown (while the systems' modules are still mapped), and the
	// global world pointer is cleared there too.
}

// -- engine stage overrides (host drives these). Initialize/Tick/Shutdown carry
// the real work; the remaining stages are empty -- FGameWorld only hosts the world
// and schedules its systems, the per-stage ECS frame runs in Tick.
void FGameWorld::PreInitialize(FEngineBase&, FEngineContext&)
{
	MAHO_TRACE_STAGE(IPreInit, "World pre-init", "nothing to build before the world exists");
}
void FGameWorld::PostInitialize(FEngineBase&, FEngineContext&)
{
	MAHO_TRACE_STAGE(IPostInit, "World post-init", "the world graphs are built on first use");
	// Nothing to build: the stage sequences are compile-time (FInputStages / FFixedStages /
	// FPostStages) and the collector's graph is created on first use. Tick drives them.
}
void FGameWorld::BeginFrame(FEngineBase&, FEngineContext&)
{
	MAHO_TRACE_STAGE(IBeginFrame, "World frame begin", "the world has no frame-head work");
}
void FGameWorld::EndFrame(FEngineBase&, FEngineContext&)
{
	MAHO_TRACE_STAGE(IEndFrame, "World frame end", "the world has no frame-tail work");
}
void FGameWorld::RequestExit(FEngineBase&, FEngineContext&)
{
	MAHO_TRACE_STAGE(IExit, "World exit", "the world has no exit work of its own");
}
void FGameWorld::PreShutdown(FEngineBase&, FEngineContext&)
{
	MAHO_TRACE_STAGE(IPreShutdown, "World pre-shutdown", "the world has no pre-teardown work");
}
void FGameWorld::PostShutdown(FEngineBase&, FEngineContext&)
{
	MAHO_TRACE_STAGE(IPostShutdown, "World post-shutdown", "the world has no post-teardown work");
}

void FGameWorld::Initialize(FEngineBase&, FEngineContext&)
{
	MAHO_TRACE_STAGE(IInit, "World init", "create the sample entity and install the systems");
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
	// FlushPendingUpdates safe point (IOnInstalled). UISystem owns the game-side
	// UI: it declares the demo widget's persistent view tree (and the draggable text-box
	// placeholder) and registers the view in the UI view registry, which the render
	// feature translates once per frame.
	Install<GameWorld::FUISystem>();

	// MeshRenderSystem is the game side of the render mirror: it owns the mesh/camera/script
	// components, drives the Lua gameplay, and publishes the frame's primitives + camera. It is
	// installed into THIS collector (the world's own), so its IOnInstalled/IUpdate/IPreUnInstall run
	// in the world's stage groups -- the render side is a different collector and reads the mirror
	// table instead (no ordering edge between the two).
	Install<GameWorld::FMeshRenderSystem>();
}

void FGameWorld::Tick(FEngineBase&, FEngineContext&)
{
	MAHO_TRACE_STAGE(ITick, "World tick", "drive the input, fixed and post stage groups");
	// THE graph this collector drives is a MEMBER of this frame (see EnsureGraph), created on first
	// use and handed to every driving call below as a parameter -- the builder stores no graph, so
	// "which graph this collector drives" is visible at each call site.
	FFrameGraph& Own = EnsureGraph();

	// Frame-start barrier: the post-fixed group is dispatched un-waited below, so it pipelines
	// across frames into this wait -- cross-frame parallelism, same as the render graph. Must
	// precede any frame-set change (a change under live nodes is a use of a freed node).
	Own.Wait();
	DrainPool();

	// Safe point for world-system install/uninstall (attach -> IOnInstalled, detach ->
	// IPreUnInstall). A changed system set is the ONLY thing that changes the frame set, and it
	// happens right here, with nothing in flight; the collector notices and re-queries.
	FlushPendingUpdates<TTypeList<IOnInstalled>, TTypeList<IPreUnInstall>>(Own);

	// Delta time.
	const auto Now = std::chrono::steady_clock::now();
	DeltaSeconds = std::chrono::duration<float>(Now - LastFrame).count();
	LastFrame = Now;

	// THIS drive's round, read ONCE for all three sequences below: the world is a collector with a
	// graph of its own, and a single round per drive is what lets its stage groups be ordered
	// against each other ("this frame's input before this frame's update") without a private
	// per-sequence counter. The number is the platform's canonical one -- see Execute's note.
	const std::uint64_t Round = Platform::GetFrameIndex();

	// Pre-fixed: resolve input once, synchronously -- simulation must read a
	// settled input state, so wait immediately after dispatch.
	Execute<FInputStages>(Own, Round);
	Own.Wait();
	DrainPool();

	// Fixed timestep: 0..N steps this frame, each strictly ordered (a step cannot
	// overlap the next, so each is waited out before the next runs).
	Accumulator += DeltaSeconds;
	while (Accumulator >= FixedStepSeconds)
	{
		Execute<FFixedStages>(Own, Round);
		Own.Wait();
		DrainPool();
		Accumulator -= FixedStepSeconds;
	}

	// Post-fixed: update + late update, dispatched WITHOUT a trailing wait so they
	// pipeline across frames; the next Tick's leading Wait (above) collects them.
	Execute<FPostStages>(Own, Round);
}

void FGameWorld::Shutdown(FEngineBase&, FEngineContext&)
{
	MAHO_TRACE_STAGE(IShutdown, "World shutdown", "drain the graph, free the pools, then uninstall all");
	// ORDER IS THE WHOLE METHOD. Each step is fenced before the next one touches memory a plugin owns:
	//   (A) drain MY in-flight work, (B) free the module-defined component pools, (C) uninstall the
	//   systems, (D) clear the global world pointer.
	//
	// THE POOLS COME OUT BEFORE THE UNINSTALL, and that is not a preference. Each pool IS a
	// `TComponentPool<C>` -- an all-inline header template with no key function -- so its vtable and
	// deleting destructor used to be the COMDAT copy emitted in whichever module CONSTRUCTED it (the
	// systems' DLLs). Destroying it through `unique_ptr<IComponentPool>` then calls a deleting
	// destructor IN THAT MODULE, and `UninstallAll` is precisely what `FreeLibrary`s it: freeing the
	// pools afterwards is a call into an unmapped image (observed: an AV inside
	// `default_delete<IComponentPool>::operator()`, reached from this method).
	//
	// The pools are therefore freed here, while the graph is fenced AND every module that could own
	// their vtable, RTTI or element deleters is still mapped. `CreateComponentPoolTyped` (ComponentPool.h)
	// additionally moves the pool's own vtable into THIS module, so the delete no longer depends on
	// which system happened to create the pool; the element destructors (`FScriptComponent::Script`,
	// `FUIWidget::View`) still live in the systems' modules, which is why the order below is required
	// either way.
	FFrameGraph& Own = EnsureGraph();

	// (A) MY OWN in-flight work first: the post-fixed group is dispatched UN-WAITED by Tick, so the
	// last tick's nodes may still be running when Shutdown arrives. They read the pools and the world
	// pointer, so they must be quiet before either is touched below.
	Own.Wait();
	DrainPool();

	// (B) The component pools: every pool and every component it holds is a module-defined object, so
	// they die HERE -- with the graph fenced (no node can be running a body that reaches them) and with
	// the systems' modules still loaded (see the order note above).
	ComponentPools.clear();
	ComponentPoolTypes.clear();
	ComponentPoolSelves.clear();

	// (C) The systems: uninstall through the collector teardown pipeline so each one's
	// IPreUnInstall runs BEFORE its instance is destroyed (a bare member destruction
	// would skip that stage and leak cross-module subscriptions -- e.g. a system's
	// std::function bound into another DLL's event). By NAME, because a name is what
	// survives a module. The flush drives that teardown as a one-shot batch on THIS graph.
	(void)UninstallAll();
	FlushPendingUpdates<TTypeList<IOnInstalled>, TTypeList<IPreUnInstall>>(Own);

	// THE OWNER'S FENCE: drain the uninstall batch (and anything its bodies submitted to this lane)
	// while the pool the graph was built with is still alive -- the graph member dies before the
	// builder base that holds the pool. ~FFrameBuilder no longer drains the graph at all (its
	// destructor is pool-only), so this IS the "the graph is quiet before anything is freed"
	// invariant, and it is the LAST use of the world's graph: nothing submits after it. No release
	// step is needed either: the member's own destructor stops the scheduler thread, which is safe
	// precisely because the nodes are all fenced here.
	Own.Wait();
	DrainPool();

	// (D) LAST, and deliberately after the fence: `GGameWorld` is read by every node that runs world
	// code (systems resolve entities through it -- e.g. the Lua bindings), so it may only be cleared
	// once no node can run any more. Clearing it earlier would make a still-running body observe a
	// null world (or miss its own guard) rather than a live one.
	GGameWorld = nullptr;
}

// ── the component-pool factory (the module boundary of the pools) ─────────────
//
// ONE LIST, TWO HALVES, and they belong together: each component type the world or a world system can
// put in a pool (a) gets an IDENTITY here (`MAHO_DECLARE_TYPE`, whose row address is the token
// `TQueryTag<T>` resolves to in every module -- that is how a pool is found again without a cast) and
// (b) gets its pool template instantiated in THIS translation unit, so each pool's vtable and
// deleting destructor belong to GameWorld.dll, the module that owns (and therefore outlives) the
// pool. CreateComponentPoolTyped's note in ComponentPool.h explains why a pool constructed in a
// system's module is a use-after-free waiting for `UninstallAll`'s FreeLibrary.
//
// ADDING A COMPONENT TYPE means adding it to BOTH halves. Forgetting the instantiation does not
// compile-link; forgetting the identity (which lives in the TYPE's own public header, so that every
// module that searches can see it) makes this static_assert fail.
} // namespace GameWorld
} // namespace Maho

namespace Maho
{
namespace GameWorld
{

template <typename C>
TComponentPool<C>* CreateComponentPoolTyped()
{
	return new TComponentPool<C>();
}

// The factory IS this list of explicit instantiations: each line instantiates the pool template HERE,
// in GameWorld.dll, so the object's vtable and deleting destructor live in the module that also owns
// (and frees) it -- see CreateComponentPoolTyped's note in ComponentPool.h.
//
// ADDING A COMPONENT TYPE means adding it here. Forgetting cannot corrupt silently: the pool is
// constructed by a TEMPLATE whose instantiations are this list, so a component type that is used but
// not listed does not LINK. The types are readable here because this cpp already includes the
// systems' headers privately (they are this plugin's PrivateIncludes); GameWorld.h stays free of them.
template TComponentPool<FTransform>*           CreateComponentPoolTyped<FTransform>();
template TComponentPool<FMeshRenderComponent>* CreateComponentPoolTyped<FMeshRenderComponent>();
template TComponentPool<FMaterialComponent>*   CreateComponentPoolTyped<FMaterialComponent>();
template TComponentPool<FCameraComponent>*     CreateComponentPoolTyped<FCameraComponent>();
template TComponentPool<FInputComponent>*      CreateComponentPoolTyped<FInputComponent>();
template TComponentPool<FScriptComponent>*     CreateComponentPoolTyped<FScriptComponent>();
template TComponentPool<FUIWidget>*            CreateComponentPoolTyped<FUIWidget>();

// The identity half of that list, checked at COMPILE time: a component type whose identity was not
// declared would resolve to a null token, and its pool would then never be found again -- silently.
static_assert(!TTypeName<FTransform>::value.empty()
	&& !TTypeName<FMeshRenderComponent>::value.empty()
	&& !TTypeName<FMaterialComponent>::value.empty()
	&& !TTypeName<FCameraComponent>::value.empty()
	&& !TTypeName<FInputComponent>::value.empty()
	&& !TTypeName<FScriptComponent>::value.empty()
	&& !TTypeName<FUIWidget>::value.empty(),
	"every component type built here needs MAHO_DECLARE_TYPE (its pool is found by that token)");

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
extern "C" MAHO_GAMEWORLD_API Maho::FFrameExtension* CreateFrame()
{
	return Maho::GameWorld::FGameWorld::CreateFrame();
}

// ── the stage access table (change add-resource-metadata-edges, step 2) ─────────────────────────
//
// The world's resource is its COMPONENT POOLS (`GameWorld::FComponentPools`, the nested tag type: the
// pools are members of the world, not a class of their own -- design D3). The rows say the lifetime
// window and who touches the pools while they exist:
//
//   IInit      -- the pools/registry come alive (the world pointer is published, the systems install):
//                 `Create`.
//   ITick      -- the frame's stage groups run and every system reads/writes components THROUGH the
//                 world: a `Write` of the pools (the world is the only writer of its own storage).
//   IShutdown  -- the pools are cleared BEFORE the systems' modules are unloaded (the COMDAT-vtable
//                 reason documented in the body), and the graph is fenced first: `Destroy`.
//
// `Read(FLog)`/`Read(IRHI)` are reads of OTHER layers' services, and they are the reason this table is
// worth having: `FLog` lives in the `FLog` frame and `IRHI` in `FRender`'s, so a teardown that touches
// them has an ordering question no hand-written edge recorded. Nothing derives an edge from these rows
// yet (that is step 4).
#include <Engine/StageAccess.h>

namespace Maho
{

MAHO_IMPLEMENT_STAGE(GameWorld::FGameWorld, IInit,
	Create(GameWorld::FComponentPools), Read(FLog))
MAHO_IMPLEMENT_STAGE(GameWorld::FGameWorld, ITick,
	Write(GameWorld::FComponentPools), Read(FLog))
MAHO_IMPLEMENT_STAGE(GameWorld::FGameWorld, IShutdown,
	Destroy(GameWorld::FComponentPools), Read(FLog), Read(IRHI))

} // namespace Maho
