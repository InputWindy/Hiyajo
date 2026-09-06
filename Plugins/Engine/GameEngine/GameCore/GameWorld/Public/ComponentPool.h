#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace Maho
{
namespace GameWorld
{

/**
 * Type-erased component pool base - lets FGameWorld hold a heterogeneous set of
 * pools keyed by component type (one TComponentPool<T> per component). Removal
 * is by entity Index; a slot is only marked dead (not compacted) so the entity's
 * Index stays addressable by every other pool, matching FEntityRegistry.
 */
class IComponentPool
{
public:
	virtual ~IComponentPool() = default;

	/** Raw access to the component at Index (nullptr when not present). */
	virtual void* GetRaw(std::uint32_t Index) = 0;

	/** Mark Index dead. */
	virtual void Remove(std::uint32_t Index) = 0;

	/** Current slot capacity (high-water mark). */
	[[nodiscard]] virtual std::uint32_t Capacity() const = 0;
};

/**
 * SoA component storage for one component type. Addressable by entity Index.
 * Add resizes the column on demand and relies on MoveAssign into a slot; a slot
 * stays put (sparse, no compaction) so component lifetime aligns with the
 * entity's Index.
 */
template <typename T>
class TComponentPool : public IComponentPool
{
public:
	[[nodiscard]] T* Get(std::uint32_t Index)
	{
		if (Index < Has.size() && Has[Index])
		{
			return &Items[Index];
		}
		return nullptr;
	}

	[[nodiscard]] const T* Get(std::uint32_t Index) const
	{
		if (Index < Has.size() && Has[Index])
		{
			return &Items[Index];
		}
		return nullptr;
	}

	/** Add (or overwrite) a component at Index; returns a pointer to the stored value. */
	T* Add(std::uint32_t Index, T Value)
	{
		if (Index >= Items.size())
		{
			const std::size_t NewSize = static_cast<std::size_t>(Index) + 1;
			Items.resize(NewSize);
			Has.resize(NewSize, false);
		}
		Items[Index] = std::move(Value);
		Has[Index] = true;
		return &Items[Index];
	}

	void* GetRaw(std::uint32_t Index) override { return Get(Index); }

	void Remove(std::uint32_t Index) override
	{
		if (Index < Has.size())
		{
			Has[Index] = false;
		}
	}

	[[nodiscard]] std::uint32_t Capacity() const override { return static_cast<std::uint32_t>(Items.size()); }

private:
	std::vector<T>      Items;   // index-aligned component values
	std::vector<bool>   Has;     // index-aligned presence mask
};

} // namespace GameWorld
} // namespace Maho
