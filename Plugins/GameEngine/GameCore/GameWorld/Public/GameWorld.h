#pragma once

#include "GameWorldApi.h"
#include "Entity.h"
#include "ComponentPool.h"
#include <Maho.h>
#include <Engine/Frame.h>
#include <Engine/FrameBuilder.h>
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

/** Per-frame state for the WORLD's stages (one instance per ring slot; reach it as
 *  `FGameWorld::FContext`).
 *
 *  DEFINED HERE, at namespace scope, and not inside the class: the stage interfaces below must
 *  name it in their signatures, and they are declared before `FGameWorld` exists. `FGameWorld`
 *  carries a nested alias so stages and the dispatch macro can still write the nested name. */
struct FGameWorldContext
{
};

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
// its own stage sequences, in the Unity ECS order below.

/** Rows/layers attached to the world (arrived via Install at the safe point). */
class MAHO_GAMEWORLD_API IOnInstalled
{
public:
	virtual ~IOnInstalled() = default;
	virtual void OnInstalled(FGameWorld&, FGameWorldContext&) = 0;
};

/** Per-frame, once, before any fixed/regular update. */
class MAHO_GAMEWORLD_API IProcessInput
{
public:
	virtual ~IProcessInput() = default;
	virtual void ProcessInput(FGameWorld&, FGameWorldContext&) = 0;
};

/** Fixed-timestep update - runs 0..N times per frame with a fixed dt. */
class MAHO_GAMEWORLD_API IFixedUpdate
{
public:
	virtual ~IFixedUpdate() = default;
	virtual void FixedUpdate(FGameWorld&, FGameWorldContext&) = 0;
};

/** Per-frame update (after all fixed steps this frame). */
class MAHO_GAMEWORLD_API IUpdate
{
public:
	virtual ~IUpdate() = default;
	virtual void Update(FGameWorld&, FGameWorldContext&) = 0;
};

/** Per-frame, after update. */
class MAHO_GAMEWORLD_API ILateUpdate
{
public:
	virtual ~ILateUpdate() = default;
	virtual void LateUpdate(FGameWorld&, FGameWorldContext&) = 0;
};

/** Detached from the world (removed at the next safe point). */
class MAHO_GAMEWORLD_API IPreUnInstall
{
public:
	virtual ~IPreUnInstall() = default;
	virtual void PreUnInstall(FGameWorld&, FGameWorldContext&) = 0;
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
	: public FFrameExtension, public IPipeline<IInit, ITick, IShutdown>
	, public FFrameBuilder<FGameWorld>
{
public:
	/** Per-frame state for the world's systems -- one instance per ring slot. (Same shape as
	 *  FEngineBase::FContext; the reasoning lives there.) EMPTY FOR NOW.
	 *
	 *  An ALIAS, not the definition: the stage interfaces must name this type before FGameWorld
	 *  exists, so the definition lives at namespace scope (see FGameWorldContext). Stages and the
	 *  dispatch macro write the nested name; it is the same type either way. */
	using FContext = FGameWorldContext;

protected:
	std::array<FContext, MAHO_FRAMES_IN_FLIGHT> Slots;

	void* GetContext(int Slot) override
	{
		return &Slots[Slot];
	}

private:
	MAHO_DECLARE_FRAME(FGameWorld);

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
	void Initialize(FEngineBase&, FEngineContext&) override;
	void Tick(FEngineBase&, FEngineContext&) override;
	void Shutdown(FEngineBase&, FEngineContext&) override;

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

	// One stage SEQUENCE per group, each carrying exactly its own stages: a batch expands one
	// node per stage in its list for every frame it is given, so a wider list would quietly run
	// those stages too (IOnInstalled/IPreUnInstall belong to the collector's install/uninstall
	// batches, not to the frame).
	//
	// The graphs themselves belong to the collector (Execute<TStages>() below), and it keeps one
	// generation counter PER SEQUENCE -- which is why this is three Execute<> calls rather than
	// one: the cross-frame self edge binds generation N to N-1, so each sequence's counter must
	// advance by one per dispatch of THAT sequence. A single shared counter would leave each
	// sequence's previous generation at another group's offset.
	using FInputStages = TTypeList<IProcessInput>;
	using FFixedStages = TTypeList<IFixedUpdate>;
	using FPostStages  = TTypeList<IUpdate, ILateUpdate>;

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
