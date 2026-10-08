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
 * Transform component - a FULL TRS, still a plain POD.
 *
 * `X/Y/Z` stay the POSITION (source compatible with the old position-only sample: `FTransform{1,2,3}`
 * still compiles and still means "position 1,2,3"). Rotation is Euler radians, scale defaults to 1.
 *
 * The COMPOSITION of these numbers into a matrix is game semantics and therefore lives on the GAME
 * side (see `MeshRenderSystem`): the object model stays a data record, and the render side only ever
 * sees the 16 floats it was handed.
 */
struct FTransform
{
	float X = 0.f;              // position
	float Y = 0.f;
	float Z = 0.f;

	float RotationX = 0.f;      // Euler radians, applied X then Y then Z (R = Rz * Ry * Rx)
	float RotationY = 0.f;
	float RotationZ = 0.f;

	float ScaleX = 1.f;         // 1 == "no scaling" (so a bare FTransform is an identity transform)
	float ScaleY = 1.f;
	float ScaleZ = 1.f;
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
	: public FFrameExtension, public IPipeline<IPreInit, IInit, IPostInit, IBeginFrame, ITick, IEndFrame, IExit, IPreShutdown, IShutdown, IPostShutdown>
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
	/** One context per ring slot, allocated ONCE at the INJECTED pipeline depth (`GetFramesInFlight()`)
	 *  on first use -- the depth is a run-time property of the process (the host injects it before the
	 *  first graph exists), so it cannot be an array bound. A fixed-size array behind a `unique_ptr`,
	 *  not a growable container: a context is not movable (it holds a mutex), so growing would have to
	 *  move slots that systems already hold references to. */
	std::unique_ptr<FContext[]> Slots;

	void* GetContext(int Slot) override
	{
		if (!Slots)
		{
			Slots = std::make_unique<FContext[]>(static_cast<std::size_t>(GetFramesInFlight()));
		}
		return &Slots[static_cast<std::size_t>(Slot)];
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
	void PreInitialize(FEngineBase&, FEngineContext&) override;
	void Initialize(FEngineBase&, FEngineContext&) override;
	void PostInitialize(FEngineBase&, FEngineContext&) override;
	void BeginFrame(FEngineBase&, FEngineContext&) override;
	void Tick(FEngineBase&, FEngineContext&) override;
	void EndFrame(FEngineBase&, FEngineContext&) override;
	void RequestExit(FEngineBase&, FEngineContext&) override;
	void PreShutdown(FEngineBase&, FEngineContext&) override;
	void Shutdown(FEngineBase&, FEngineContext&) override;
	void PostShutdown(FEngineBase&, FEngineContext&) override;

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
	// The graphs themselves belong to the collector (Execute<TStages>(Frame) below), and the ROUND
	// comes from OUTSIDE -- the host reads the canonical frame number once per frame and passes the
	// same value to all three calls. That is what keeps one tick's sequences in one slot: three
	// per-sequence counters would each have a private notion of "the previous frame", and the
	// cross-frame self edge (which binds a stage to ITSELF one round back) would then resolve into
	// three different rounds.
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
