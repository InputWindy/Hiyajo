#pragma once

#include <cstdint>

namespace Maho
{

/**
 * Lightweight entity handle. Index/generation pair ensures dangling references
 * are detected on lookup (generation mismatch → invalid).
 */
struct FEntityHandle
{
	static constexpr std::uint32_t InvalidIndex = 0xFFFFFF;
	static constexpr std::uint8_t InvalidGeneration = 0xFF;

	std::uint32_t Index : 24 = InvalidIndex;
	std::uint32_t Generation : 8 = InvalidGeneration;

	[[nodiscard]] bool IsValid() const
	{
		return Index != InvalidIndex && Generation != InvalidGeneration;
	}

	[[nodiscard]] bool operator==(const FEntityHandle& Other) const
	{
		return Index == Other.Index && Generation == Other.Generation;
	}

	[[nodiscard]] bool operator!=(const FEntityHandle& Other) const
	{
		return !(*this == Other);
	}

	static FEntityHandle Make(std::uint32_t InIndex, std::uint8_t InGeneration)
	{
		FEntityHandle H;
		H.Index = InIndex;
		H.Generation = InGeneration;
		return H;
	}
};

} // namespace Maho
