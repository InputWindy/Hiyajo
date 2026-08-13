#pragma once

#include <Core/Export.h>
#include <Core/Extension/World/ECS/ComponentType.h>
#include <Core/Extension/World/ECS/EntityHandle.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Maho
{

constexpr std::size_t ECSChunkSize = 16 * 1024; // 16 KB

/**
 * One entity row in a chunk.
 */
struct FEntityRow
{
	FEntityHandle Handle;
};

/**
 * SoA storage for entities sharing the same archetype.
 *
 * Layout:
 *   [FEntityRow * N]  — entity handles at front
 *   [CompCol_0 * N]   — component A column (skip if tag)
 *   [CompCol_1 * N]   — component B column (skip if tag)
 *   ...
 *
 * Tag components (sizeof(T) == 0) occupy a mask bit but allocate no column.
 */
struct MAHO_API FChunk
{
	static constexpr std::size_t Capacity = ECSChunkSize;

	/**
	 * @param InMask         Full component mask for this archetype (includes tags)
	 * @param InComponentSizes Per-component byte sizes (0 for tags)
	 */
	FChunk(const ComponentMaskType& InMask, const std::vector<std::size_t>& InComponentSizes);

	FChunk(const FChunk&) = delete;
	FChunk& operator=(const FChunk&) = delete;
	FChunk(FChunk&& Other) noexcept;
	FChunk& operator=(FChunk&& Other) noexcept;

	~FChunk();

	[[nodiscard]] bool IsFull() const { return Count >= MaxCount; }
	[[nodiscard]] bool IsEmpty() const { return Count == 0; }

	/**
	 * Raw pointer to component column for a given type index.
	 * Returns nullptr for tag components.
	 */
	[[nodiscard]] void* GetComponentColumn(std::size_t TypeIndex);
	[[nodiscard]] const void* GetComponentColumn(std::size_t TypeIndex) const;

	/**
	 * Pointer to component data for a specific row.
	 * Asserts if T is a tag component (zero size).
	 */
	template <typename T>
	[[nodiscard]] T* GetComponent(std::size_t Row)
	{
		static_assert(!IsTagComponent<T>, "GetComponent: T is a tag component, use HasTag instead");
		return reinterpret_cast<T*>(static_cast<char*>(GetComponentColumn(GetComponentTypeId<T>())) + Row * sizeof(T));
	}

	template <typename T>
	[[nodiscard]] const T* GetComponent(std::size_t Row) const
	{
		static_assert(!IsTagComponent<T>, "GetComponent: T is a tag component, use HasTag instead");
		return reinterpret_cast<const T*>(static_cast<const char*>(GetComponentColumn(GetComponentTypeId<T>())) + Row * sizeof(T));
	}

	[[nodiscard]] FEntityRow* GetEntityRows() { return reinterpret_cast<FEntityRow*>(Data); }
	[[nodiscard]] const FEntityRow* GetEntityRows() const { return reinterpret_cast<const FEntityRow*>(Data); }

	ComponentMaskType Mask;
	std::vector<std::size_t> ComponentSizes;     // byte sizes per type index (0 = tag)
	std::vector<std::size_t> ColumnOffsets;      // byte offsets from Data for data columns only
	std::size_t MaxCount = 0;
	std::size_t Count = 0;
	std::uint8_t* Data = nullptr;
};

} // namespace Maho
