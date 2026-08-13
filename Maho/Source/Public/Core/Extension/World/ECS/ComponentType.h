#pragma once

#include <Core/Export.h>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace Maho
{

constexpr std::size_t ECSMaxComponentTypes = 64;
using ComponentMaskType = std::bitset<ECSMaxComponentTypes>;

using FComponentTypeId = std::uint32_t;

namespace Internal
{
	MAHO_API FComponentTypeId NextComponentTypeId();

	MAHO_API std::vector<std::size_t>& GetComponentSizeRegistry();

	MAHO_API void RegisterComponentSize(FComponentTypeId Id, std::size_t Size);

	MAHO_API [[nodiscard]] std::size_t GetComponentSize(FComponentTypeId Id);
}

/**
 * Returns a unique component type id for T.
 * Works for both data components (sizeof(T) > 0) and tag components (sizeof(T) == 0).
 */
template <typename T>
[[nodiscard]] FComponentTypeId GetComponentTypeId()
{
	static FComponentTypeId Id = []()
	{
		FComponentTypeId NewId = Internal::NextComponentTypeId();
		Internal::RegisterComponentSize(NewId, sizeof(T));
		return NewId;
	}();
	return Id;
}

/**
 * Trait to detect tag components (zero-size empty structs).
 */
template <typename T>
constexpr bool IsTagComponent = std::is_empty_v<T> && std::is_trivially_copyable_v<T>;

/**
 * Build a component mask from a variadic list of types.
 */
template <typename... Ts>
[[nodiscard]] ComponentMaskType MakeComponentMask()
{
	ComponentMaskType Mask;
	((Mask.set(GetComponentTypeId<Ts>())), ...);
	return Mask;
}

} // namespace Maho
