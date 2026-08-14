#pragma once

#include <Core/Misc/Export.h>
#include <Core/Extension/World/ECS/Chunk.h>

#include <cstddef>
#include <vector>

namespace Maho
{

/**
 * Archetype = fixed component signature (mask + sizes).
 * Owns a list of FChunk instances sharing the same signature.
 *
 * Tag components are tracked in Mask but consume zero Chunk space.
 */
struct MAHO_API FArchetype
{
	FArchetype(const ComponentMaskType& InMask, const std::vector<std::size_t>& InComponentSizes);

	FArchetype(const FArchetype&) = delete;
	FArchetype& operator=(const FArchetype&) = delete;
	FArchetype(FArchetype&&) noexcept = default;
	FArchetype& operator=(FArchetype&&) noexcept = default;

	~FArchetype();

	[[nodiscard]] bool MatchesMask(const ComponentMaskType& Other) const
	{
		return Mask == Other;
	}

	/** Allocate and push a new chunk of this archetype. */
	FChunk* AllocateChunk();

	ComponentMaskType Mask;
	std::vector<std::size_t> ComponentSizes; // per-type-index byte sizes (0 = tag)
	std::vector<FChunk*> Chunks;
};

} // namespace Maho
