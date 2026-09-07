#pragma once

#include "GameWorldApi.h"
#include "Entity.h"
#include "ComponentPool.h"
#include <Maho.h>
#include <Engine/Layer.h>
#include <Engine/LayerCollector.h>
#include <Engine/LayerTaskGraph.h>
#include <Engine/Engine.h>
#include <Core/TypeList.h>

#include <chrono>
#include <memory>
#include <utility>
#include <vector>

namespace Maho
{
namespace GameWorld
{

class FGameWorld;

/**
 * Sample component (demonstrates the object model; add your own component structs
 * / classes in your systems).
 */
struct FTransform
{
	float X = 0.f;
	float Y = 0.f;
	float Z = 0.f;
};

// -- ECS world sub-stage interfaces (implemented by the world's systems) ----------
// FGameWorld is a sub-landlord (二房东): it schedules its INSTALLED systems through
// its own FLayerTaskGraph, in the Unity ECS order below.

/** Rows/layers attached to the world (arrived via Install at the safe point). */
class MAHO_GAMEWORLD_API IOnInstalled
{
public:
	virtual ~IOnInstalled() = default;
	virtual void OnInstalled(FGameWorld&) = 0;
};

/** Per-frame, once, before any fixed/regular update. */
class MAHO_GAMEWORLD_API IProcessInput
{
public:
	virtual ~IProcessInput() = default;
	virtual void ProcessInput(FGameWorld&) = 0;
};

/** Fixed-timestep update - runs 0..N times per frame with a fixed dt. */
class MAHO_GAMEWORLD_API IFixedUpdate
{
public:
	virtual ~IFixedUpdate() = default;
	virtual void FixedUpdate(FGameWorld&) = 0;
};

/** Per-frame update (after all fixed steps this frame). */
class MAHO_GAMEWORLD_API IUpdate
{
public:
	virtual ~IUpdate() = default;
	virtual void Update(FGameWorld&) = 0;
};

/** Per-frame, after update. */
class MAHO_GAMEWORLD_API ILateUpdate
{
public:
	virtual ~ILateUpdate() = default;
	virtual void LateUpdate(FGameWorld&) = 0;
};

/** Detached from the world (removed at the next safe point). */
class MAHO_GAMEWORLD_API IPreUnInstall
{
public:
	virtual ~IPreUnInstall() = default;
	virtual void PreUnInstall(FGameWorld&) = 0;
};

/**
 * Game world subsystem - an engine layer (mounted as IInit/ITick/IShutdown on the
 * host) plus its own layer collector for WORLD SYSTEMS (implementing the ECS
 * sub-stages above). This is the ECS "System" container: FGameWorld schedules the
 * systems through a per-stage task graph, in Unity order (process input -> fixed
 * update xN -> update -> late update), driven from its Tick.
 *
 * The ECS object model lives here: FEntity (generation handle) + TComponentPool
 * (SoA, index-aligned) + FEntityRegistry. Systems are installed as peer layers
 * via Install<...>() and receive the world (FGameWorld&) at each stage.
 */
class MAHO_GAMEWORLD_API FGameWorld
	: public FLayer<IPreInit, IInit, IPostInit, IBeginFrame, ITick, IEndFrame, IExit, IPreShutdown, IShutdown, IPostShutdown>
	, public FLayerCollector<FGameWorld>
{
	MAHO_DECLARE_LAYER(FGameWorld, "GameWorld.dll");

public:
	FGameWorld();
	~FGameWorld() override;

	// -- ECS object model --------------------------------------------------------
	[[nodiscard]] FEntity CreateEntity();
	bool DestroyEntity(FEntity E);
	[[nodiscard]] bool IsAlive(FEntity E) const;

	[[nodiscard]] FEntityRegistry& GetRegistry() { return Registry; }

	template <typename C>
	C* AddComponent(FEntity E, C Value)
	{
		return GetOrAddPool<C>()->Add(E.Index, std::move(Value));
	}

	template <typename C>
	[[nodiscard]] C* GetComponent(FEntity E)
	{
		TComponentPool<C>* Pool = GetPool<C>();
		return Pool ? Pool->Get(E.Index) : nullptr;
	}

	template <typename C>
	void RemoveComponent(FEntity E)
	{
		if (TComponentPool<C>* Pool = GetPool<C>())
		{
			Pool->Remove(E.Index);
		}
	}

	/** Collect every alive entity that has a component of type C (any owner --
	 *  world or a system). Used by renderers/visualizers to iterate the full set
	 *  of, e.g., FUIWidget entities regardless of who created them. */
	template <typename C>
	std::vector<FEntity> GetAllWithComponent()
	{
		std::vector<FEntity> Out;
		if (TComponentPool<C>* Pool = GetPool<C>())
		{
			for (std::uint32_t I = 0; I < Pool->Capacity(); ++I)
			{
				if (Pool->Get(I) != nullptr)
				{
					Out.push_back(FEntity{ I, 0u });
				}
			}
		}
		return Out;
	}

	// -- frame loop driver -------------------------------------------------------
	void SetFixedStep(float Seconds) { FixedStepSeconds = Seconds; }
	[[nodiscard]] float GetFixedStep() const { return FixedStepSeconds; }
	[[nodiscard]] float GetDeltaSeconds() const { return DeltaSeconds; }

private:
	// engine stage overrides (the host drives these; the ECS frame runs in Tick).
	// FGameWorld inherits all ten engine stages and overrides every one (PerFrame etc.
	// are empty) -- the stage interfaces are pure virtual, so all must be implemented.
	void PreInitialize(FEngineBase&) override;
	void Initialize(FEngineBase&) override;
	void PostInitialize(FEngineBase&) override;
	void BeginFrame(FEngineBase&) override;
	void Tick(FEngineBase&) override;
	void EndFrame(FEngineBase&) override;
	void RequestExit(FEngineBase&) override;
	void PreShutdown(FEngineBase&) override;
	void Shutdown(FEngineBase&) override;
	void PostShutdown(FEngineBase&) override;

	// Build + dispatch one stage group through the graph, WITHOUT flushing. The
	// caller decides where the barrier goes: pre-fixed groups flush immediately,
	// the trailing group stays async so it pipelines across frames.
	template <typename... TStages>
	bool ExecuteGraph();

	template <typename C>
	TComponentPool<C>* GetOrAddPool()
	{
		for (const auto& Pool : ComponentPools)
		{
			if (auto* Typed = dynamic_cast<TComponentPool<C>*>(Pool.get()))
			{
				return Typed;
			}
		}
		auto New = std::make_unique<TComponentPool<C>>();
		TComponentPool<C>* Raw = New.get();
		ComponentPools.push_back(std::move(New));
		return Raw;
	}

	template <typename C>
	TComponentPool<C>* GetPool()
	{
		for (const auto& Pool : ComponentPools)
		{
			if (auto* Typed = dynamic_cast<TComponentPool<C>*>(Pool.get()))
			{
				return Typed;
			}
		}
		return nullptr;
	}

	using FWorldStages = TTypeList<IOnInstalled, IProcessInput, IFixedUpdate, IUpdate, ILateUpdate, IPreUnInstall>;
	std::unique_ptr<FLayerTaskGraph<FWorldStages, FGameWorld>> WorldGraph;

	FEntityRegistry Registry;
	std::vector<std::unique_ptr<IComponentPool>> ComponentPools;   // one TComponentPool<T> per component type

	std::chrono::steady_clock::time_point LastFrame = std::chrono::steady_clock::now();
	float DeltaSeconds = 0.f;
	float FixedStepSeconds = 1.f / 60.f;
	float Accumulator = 0.f;
};

/** Global world accessor (cross-DLL via function). Set when the world initializes. */
MAHO_GAMEWORLD_API FGameWorld* GetGameWorld();

} // namespace GameWorld

// Stage dispatch specializations for the GameWorld context. These MUST be at the
// Maho namespace scope (not nested GameWorld) so they full-specialize the primary
// Invoke<TStage, TContext> template declared in Layer.h -- same pattern as Engine.h
// (FEngineBase) and Render.h (FRender).
MAHO_DECLARE_STAGE_DISPATCH(GameWorld::FGameWorld,   GameWorld::IOnInstalled,   GameWorld::IOnInstalled,   OnInstalled)
MAHO_DECLARE_STAGE_DISPATCH(GameWorld::FGameWorld,   GameWorld::IProcessInput,  GameWorld::IProcessInput,  ProcessInput)
MAHO_DECLARE_STAGE_DISPATCH(GameWorld::FGameWorld,   GameWorld::IFixedUpdate,   GameWorld::IFixedUpdate,   FixedUpdate)
MAHO_DECLARE_STAGE_DISPATCH(GameWorld::FGameWorld,   GameWorld::IUpdate,        GameWorld::IUpdate,        Update)
MAHO_DECLARE_STAGE_DISPATCH(GameWorld::FGameWorld,   GameWorld::ILateUpdate,    GameWorld::ILateUpdate,    LateUpdate)
MAHO_DECLARE_STAGE_DISPATCH(GameWorld::FGameWorld,   GameWorld::IPreUnInstall,  GameWorld::IPreUnInstall,  PreUnInstall)

} // namespace Maho
