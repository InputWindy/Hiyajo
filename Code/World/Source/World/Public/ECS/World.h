#pragma once

#include "WorldApi.h"
#include <ECS/EntityManager.h>
#include <ECS/Query.h>

#include <cstdint>
#include <vector>

namespace Maho
{

/**
 * FWorld is the top-level ECS container — pure data (entities + components).
 *
 * No tick interface. Systems (ISystem) drive all per-frame logic;
 * an owning layer maps engine stages to SystemGroup hooks.
 *
 * Usage:
 *   FWorld World;
 *   FEntityHandle H = World.CreateEntity();
 *   World.SetComponent<FTransformComponent>(H, Transform);
 *   World.Query<FTransformComponent>().ForEach(...);
 */
class MAHO_WORLD_API FWorld
{
public:
	FWorld();
	virtual ~FWorld() = default;

	FWorld(const FWorld&) = delete;
	FWorld& operator=(const FWorld&) = delete;
	FWorld(FWorld&&) = delete;
	FWorld& operator=(FWorld&&) = delete;

	// ─── EntityManager access ─────────────────────────────────────

	[[nodiscard]] FEntityManager& GetEntityManager();
	[[nodiscard]] const FEntityManager& GetEntityManager() const;

	// ─── Convenience: direct entity manipulation ──────────────────

	FEntityHandle CreateEntity();

	template <typename T>
	void SetComponent(FEntityHandle Handle, const T& Value)
	{
		Manager.SetComponent<T>(Handle, Value);
	}

	template <typename T>
	void AddTag(FEntityHandle Handle)
	{
		Manager.AddTag<T>(Handle);
	}

	template <typename T>
	void RemoveTag(FEntityHandle Handle)
	{
		Manager.RemoveTag<T>(Handle);
	}

	// ─── Convenience: query builder ────────────────────────────────

	template <typename... Ts>
	TComponentQuery<Ts...> Query()
	{
		TComponentQuery<Ts...> Q;
		Q.Gather(Manager);
		return Q;
	}

private:
	FEntityManager Manager;
};

} // namespace Maho
