#pragma once

#include <Core/Misc/Export.h>
#include <Core/Engine/EngineExtension.h>
#include <Core/Engine/EngineStage.h>
#include <Core/Extension/World/ECS/System.h>
#include <Core/Extension/World/ECS/World.h>

#include <memory>
#include <string>
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
 * The root group doubles as the engine extension: it owns the FWorld (pure data)
 * and maps EEngineStage to the ISystem lifecycle hooks, recursively driving all
 * nested groups + systems. Each group automatically creates Begin/End ECB systems.
 */
class MAHO_API FSystemGroup : public ISystem, public IEngineExtension
{
public:
	explicit FSystemGroup(const char* InName);
	virtual ~FSystemGroup();

	const char* GetName() const override { return Name.c_str(); }

	// ── IEngineExtension ───────────────────────────────────────────

	/** Map engine stages to the ISystem lifecycle hooks (drives the whole tree). */
	bool ExecuteStage(EEngineStage Stage) override;

	// ── World access ───────────────────────────────────────────────

	[[nodiscard]] FWorld& GetWorld() { return World; }
	[[nodiscard]] const FWorld& GetWorld() const { return World; }

	// ── Project hooks ──────────────────────────────────────────────

	/** Register game systems into the simulation group during Attach. */
	virtual void RegisterSystems(FSystemGroup& SimGroup) { (void)SimGroup; }

	/** Spawn initial entities (camera, demo actors, etc.) during Attach. */
	virtual void SpawnInitialEntities(FWorld& World) { (void)World; }

	// ── Multi-stage lifecycle ──────────────────────────────────────

	void OnCreate(FWorld& World) override;
	void OnDestroy(FWorld& World) override;
	void OnBeginFrame(FWorld& World) override;
	void OnFixedUpdate(float DeltaTime, FWorld& World) override;
	void OnUpdate(float DeltaTime, FWorld& World) override;
	void OnLateUpdate(float DeltaTime, FWorld& World) override;
	void OnEndFrame(FWorld& World) override;
	void OnPreRender(FWorld& World) override;
	void OnPostRender(FWorld& World) override;

	// ── System registration ────────────────────────────────────────
	template <typename T, typename... Args>
	T* AddSystem(Args&&... InArgs)
	{
		static_assert(std::is_base_of_v<ISystem, T>, "T must derive from ISystem");
		auto Sys = std::make_unique<T>(std::forward<Args>(InArgs)...);
		T* Ptr = Sys.get();
		OwnedChildren.push_back(std::move(Sys));
		Systems.push_back(Ptr);
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

private:
	template <typename TMethod>
	void DispatchNoDT(FWorld& World, TMethod Method)
	{
		for (FSystemGroup* Sub : Groups)
		{
			if (Sub) { (Sub->*Method)(World); }
		}
		for (ISystem* Sys : Systems)
		{
			if (Sys) { (Sys->*Method)(World); }
		}
	}

	template <typename TMethod>
	void DispatchWithDT(FWorld& World, float DeltaTime, TMethod Method)
	{
		for (FSystemGroup* Sub : Groups)
		{
			if (Sub) { (Sub->*Method)(DeltaTime, World); }
		}
		for (ISystem* Sys : Systems)
		{
			if (Sys) { (Sys->*Method)(DeltaTime, World); }
		}
	}

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
class MAHO_API FInitializationSystemGroup : public FSystemGroup
{
public:
	FInitializationSystemGroup() : FSystemGroup("InitializationSystemGroup") {}
	static const char* StaticName() { return "InitializationSystemGroup"; }
};

class MAHO_API FSimulationSystemGroup : public FSystemGroup
{
public:
	FSimulationSystemGroup() : FSystemGroup("SimulationSystemGroup") {}
	static const char* StaticName() { return "SimulationSystemGroup"; }
};

class MAHO_API FPresentationSystemGroup : public FSystemGroup
{
public:
	FPresentationSystemGroup() : FSystemGroup("PresentationSystemGroup") {}
	static const char* StaticName() { return "PresentationSystemGroup"; }
};

} // namespace Maho
