#pragma once

#include "WorldApi.h"
#include <Core/Engine/EngineExtension.h>
#include <Core/Engine/EngineStage.h>
#include <ECS/System.h>
#include <ECS/World.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Maho
{

class FEntityCommandBuffer;

/**
 * A group of systems that executes in dependency-sorted order.
 *
 * FSystemGroup itself is an ISystem, enabling deep nesting:
 *
 *   FInitializationSystemGroup
 *     └─ FSimulationSystemGroup
 *          ├─ FMovementSystem
 *          └─ FDeathSystem
 *
 * The root group owns the FWorld (pure data) and dispatches lifecycle
 * EEngineStage values through ExecuteStage; the per-frame game world is driven
 * by the public BeginFrame / Tick / EndFrame methods. Each group automatically
 * creates Begin/End ECB systems ordered first/last in its Systems list.
 */
class MAHO_WORLD_API FSystemGroup : public ISystem, public IEngineExtension
{
public:
	explicit FSystemGroup(const char* InName);
	virtual ~FSystemGroup();

	const char* GetName() const override { return Name.c_str(); }

	// ── IEngineExtension entry (the engine drives the root group) ──
	bool ExecuteStage(EEngineStage Stage) override;

	// ── Game world frame (driven by FGameEngine, not by ExecuteStage) ──
	void BeginFrame();
	void Tick();
	void EndFrame();

	// ── ISystem dispatch (parent group drives children) ────────────
	void OnCreate(FWorld& InWorld) override
	{
		for (ISystem* S : Systems)
		{
			if (S)
			{
				S->OnCreate(InWorld);
			}
		}
	}

	void OnDestroy(FWorld& InWorld) override
	{
		for (ISystem* S : Systems)
		{
			if (S)
			{
				S->OnDestroy(InWorld);
			}
		}
	}

	void OnBeginFrame(FWorld& InWorld) override
	{
		for (ISystem* S : Systems)
		{
			if (S)
			{
				S->OnBeginFrame(InWorld);
			}
		}
	}

	void OnProcessInput(FWorld& InWorld) override
	{
		for (ISystem* S : Systems)
		{
			if (S)
			{
				S->OnProcessInput(InWorld);
			}
		}
	}

	void OnFixedUpdate(float DeltaTime, FWorld& InWorld) override
	{
		for (ISystem* S : Systems)
		{
			if (S)
			{
				S->OnFixedUpdate(DeltaTime, InWorld);
			}
		}
	}

	void OnUpdate(float DeltaTime, FWorld& InWorld) override
	{
		for (ISystem* S : Systems)
		{
			if (S)
			{
				S->OnUpdate(DeltaTime, InWorld);
			}
		}
	}

	void OnLateUpdate(float DeltaTime, FWorld& InWorld) override
	{
		for (ISystem* S : Systems)
		{
			if (S)
			{
				S->OnLateUpdate(DeltaTime, InWorld);
			}
		}
	}

	void OnEndFrame(FWorld& InWorld) override
	{
		for (ISystem* S : Systems)
		{
			if (S)
			{
				S->OnEndFrame(InWorld);
			}
		}
	}

	// ── World access (the root group owns the world data) ──────────

	[[nodiscard]] FWorld& GetWorld() { return World; }
	[[nodiscard]] const FWorld& GetWorld() const { return World; }

	// ── Project hooks ──────────────────────────────────────────────

	/** Register game systems into the simulation group during Attach. */
	virtual void RegisterSystems(FSystemGroup& SimGroup) { (void)SimGroup; }

	/** Spawn initial entities (camera, demo actors, etc.) during Attach. */
	virtual void SpawnInitialEntities(FWorld& World) { (void)World; }

	// ── System registration ────────────────────────────────────────
	template <typename T, typename... Args>
	T* AddSystem(Args&&... InArgs)
	{
		static_assert(std::is_base_of_v<ISystem, T>, "T must derive from ISystem");
		auto Sys = std::make_unique<T>(std::forward<Args>(InArgs)...);
		T* Ptr = Sys.get();
		OwnedChildren.push_back(std::move(Sys));
		InsertBeforeEnd(Ptr);
		return Ptr;
	}

	/** Add a sub-group to this group (takes ownership). */
	template <typename T, typename... Args>
	T* AddGroup(Args&&... InArgs)
	{
		static_assert(std::is_base_of_v<FSystemGroup, T>, "T must derive from FSystemGroup");
		auto Grp = std::make_unique<T>(std::forward<Args>(InArgs)...);
		T* Ptr = Grp.get();
		OwnedGroups.push_back(std::move(Grp));
		Groups.push_back(Ptr);
		InsertBeforeEnd(Ptr);
		return Ptr;
	}

	/** Declare that A must update before B within this group. */
	template <typename A, typename B>
	void UpdateBefore()
	{
		UpdateBeforeByName(A::StaticName(), B::StaticName());
	}

	/** Declare that A must update after B within this group. */
	template <typename A, typename B>
	void UpdateAfter()
	{
		UpdateBefore<B, A>();
	}

	/** Access the Begin ECB for this group. */
	FEntityCommandBuffer& GetBeginECB() { return *BeginECB; }

	/** Access the End ECB for this group. */
	FEntityCommandBuffer& GetEndECB() { return *EndECB; }

	/** Static name helper: override in subclasses. */
	static const char* StaticName() { return "SystemGroup"; }

protected:
	void UpdateBeforeByName(const char* A, const char* B);

	/** True when this group is the root that owns the world + builds the tree. */
	[[nodiscard]] virtual bool IsRootGroup() const { return false; }

private:
	void InsertBeforeEnd(ISystem* InSystem);

	std::string Name;

	/** World data owned by the root group (pure data, no tick interface). */
	FWorld World;
	bool bWorldReady = false;

	std::vector<ISystem*> Systems;
	std::vector<FSystemGroup*> Groups;

	std::vector<std::unique_ptr<ISystem>> OwnedChildren;
	std::vector<std::unique_ptr<FSystemGroup>> OwnedGroups;

	std::unique_ptr<FEntityCommandBuffer> BeginECB;
	std::unique_ptr<FEntityCommandBuffer> EndECB;
	std::unique_ptr<ISystem> BeginECBSystem;
	std::unique_ptr<ISystem> EndECBSystem;
};

/**
 * Built-in system groups matching Unity DOTS conventions.
 */
class MAHO_WORLD_API FInitializationSystemGroup : public FSystemGroup
{
public:
	FInitializationSystemGroup() : FSystemGroup("InitializationSystemGroup") {}
	static const char* StaticName() { return "InitializationSystemGroup"; }

protected:
	[[nodiscard]] bool IsRootGroup() const override { return true; }
};

class MAHO_WORLD_API FSimulationSystemGroup : public FSystemGroup
{
public:
	FSimulationSystemGroup() : FSystemGroup("SimulationSystemGroup") {}
	static const char* StaticName() { return "SimulationSystemGroup"; }
};

class MAHO_WORLD_API FPresentationSystemGroup : public FSystemGroup
{
public:
	FPresentationSystemGroup() : FSystemGroup("PresentationSystemGroup") {}
	static const char* StaticName() { return "PresentationSystemGroup"; }
};

} // namespace Maho
