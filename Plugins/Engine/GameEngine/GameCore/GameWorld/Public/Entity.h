#pragma once

#include <cstdint>
#include <vector>

namespace Maho
{
namespace GameWorld
{

/**
 * Entity handle - a (Index, Generation) pair. Index addresses a slot in the
 * registry; Generation is bumped on destroy so a stale handle (carrying an older
 * generation) never lands on a recycled slot. A default FEntity is invalid
 * (Index == 0xFFFFFFFF).
 */
struct FEntity
{
	std::uint32_t Index = 0xFFFFFFFFu;
	std::uint32_t Generation = 0u;

	[[nodiscard]] bool IsValid() const { return Index != 0xFFFFFFFFu; }

	bool operator==(const FEntity&) const = default;
	bool operator!=(const FEntity&) const = default;
};

/**
 * Entity identity registry - index-aligned header array + free list.
 * A slot is reused (its generation bumped) rather than the array being
 * compacted, so an entity's Index is stable for its whole lifetime and every
 * component pool can address a component by the same Index.
 */
class FEntityRegistry
{
public:
	/** Allocate a fresh entity slot (reuse a freed one when possible). */
	FEntity Create()
	{
		if (!FreeList.empty())
		{
			const std::uint32_t Idx = FreeList.back();
			FreeList.pop_back();
			FEntityHeader& H = Headers[Idx];
			H.bAlive = true;
			return FEntity{Idx, H.Generation};
		}
		const std::uint32_t Idx = static_cast<std::uint32_t>(Headers.size());
		Headers.push_back(FEntityHeader{0u, true});
		return FEntity{Idx, 0u};
	}

	/** Destroy an entity. Returns false on a stale handle (wrong generation) or an
	 *  invalid/already-dead slot. On success the slot is freed + generation bumped. */
	bool Destroy(FEntity E)
	{
		if (E.Index >= Headers.size())
		{
			return false;
		}
		FEntityHeader& H = Headers[E.Index];
		if (!H.bAlive || H.Generation != E.Generation)
		{
			return false;
		}
		H.bAlive = false;
		++H.Generation;   // invalidate every stale handle to this slot
		FreeList.push_back(E.Index);
		return true;
	}

	[[nodiscard]] bool IsAlive(FEntity E) const
	{
		return E.Index < Headers.size()
			&& Headers[E.Index].bAlive
			&& Headers[E.Index].Generation == E.Generation;
	}

	[[nodiscard]] std::uint32_t GetCapacity() const { return static_cast<std::uint32_t>(Headers.size()); }

private:
	struct FEntityHeader
	{
		std::uint32_t Generation;
		bool bAlive;
	};

	std::vector<FEntityHeader> Headers;      // index-aligned slot headers
	std::vector<std::uint32_t> FreeList;     // freed slot indices for reuse
};

} // namespace GameWorld
} // namespace Maho
