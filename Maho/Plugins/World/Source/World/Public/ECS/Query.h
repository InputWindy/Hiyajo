#pragma once

#include <ECS/Archetype.h>
#include <ECS/ComponentType.h>
#include <ECS/EntityHandle.h>

#include <cstddef>
#include <vector>

namespace Maho
{

/**
 * ChunkView: iterates over all entity rows within a single chunk that match
 * the component mask. Returns entity handle + component refs.
 */
template <typename... Ts>
struct TChunkView
{
	FChunk* Chunk = nullptr;

	[[nodiscard]] std::size_t Count() const { return Chunk ? Chunk->Count : 0; }

	template <typename F>
	void ForEach(F&& Func)
	{
		if (Chunk == nullptr)
		{
			return;
		}
		FEntityRow* Rows = Chunk->GetEntityRows();
		for (std::size_t Row = 0; Row < Chunk->Count; ++Row)
		{
			Func(Rows[Row].Handle, *Chunk->GetComponent<Ts>(Row)...);
		}
	}
};

/**
 * Query builder over entities.
 *
 * Usage:
 *   auto Q = World.Query<FPosition, FVelocity>().Not<FDeadTag>();
 *   Q.ForEach([](FEntityHandle H, FPosition& Pos, const FVelocity& Vel) { ... });
 *
 * Optional tags (With<>):
 *   auto Q = World.Query<FPosition>().With<FSelectedTag>();
 */
template <typename... Ts>
struct TComponentQuery
{
	std::vector<TChunkView<Ts...>> Views;

	void Gather(const class FEntityManager& Manager);

	/** Exclude entities that have any of the given components/tags. */
	template <typename... Us>
	TComponentQuery& Not()
	{
		ExcludedMask |= MakeComponentMask<Us...>();
		return *this;
	}

	/** Require entities to have these additional components/tags (already gathered, just filter). */
	template <typename... Us>
	TComponentQuery& With()
	{
		OptionalMask |= MakeComponentMask<Us...>();
		return *this;
	}

	template <typename F>
	void ForEach(F&& Func)
	{
		for (auto& View : Views)
		{
			// With<>: skip chunks whose archetype lacks the optional mask.
			if (OptionalMask.any())
			{
				if ((View.Chunk->Mask & OptionalMask) != OptionalMask)
				{
					continue;
				}
			}
			// Not<>: skip chunks whose archetype contains any excluded component.
			if (ExcludedMask.any())
			{
				if ((View.Chunk->Mask & ExcludedMask).any())
				{
					continue;
				}
			}
			View.ForEach(std::forward<F>(Func));
		}
	}

	ComponentMaskType ExcludedMask;
	ComponentMaskType OptionalMask;
};

} // namespace Maho
