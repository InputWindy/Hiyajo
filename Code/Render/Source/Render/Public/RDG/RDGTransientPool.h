#pragma once

#include "RenderApi.h"
#include <RHI/RHIResources.h>

#include <cstdint>
#include <vector>

namespace Maho
{

class IRHI;

/**
 * Per-frame transient resource pool with greedy memory-aliasing.
 *
 * Transient resources created by RDG (CreateBuffer/CreateTexture) are allocated
 * through this pool. Resources whose lifetimes do not overlap may share
 * the same backing VkBuffer/VkImage.
 *
 * Slot RHI resources are NOT destroyed on Reset() — they persist across frames
 * and are only re-created when the descriptor changes.
 */
class MAHO_RENDER_API FRDGTransientPool
{
public:
	FRDGTransientPool() = default;
	~FRDGTransientPool();
	FRDGTransientPool(const FRDGTransientPool&) = delete;
	FRDGTransientPool& operator=(const FRDGTransientPool&) = delete;

	/**
	 * Allocate (or reuse) a buffer for the pass range [FirstUse, LastUse].
	 */
	FRHIBuffer* AllocateBuffer(IRHI* RHI,
	                           const FRHIBufferDesc& Desc,
	                           std::uint32_t FirstUse,
	                           std::uint32_t LastUse);

	/**
	 * Allocate (or reuse) a texture for [FirstUse, LastUse].
	 */
	FRHITexture* AllocateTexture(IRHI* RHI,
	                             const FRHITextureDesc& Desc,
	                             std::uint32_t FirstUse,
	                             std::uint32_t LastUse);

	/**
	 * End-of-frame: mark all slots as available for next frame.
	 * Does NOT free VkBuffer/VkImage; slot reuse is descriptor-keyed.
	 */
	void Reset();

	/** Destroy all pooled RHI resources (called on shutdown). */
	void Shutdown(IRHI* RHI);

private:
	struct FSlot
	{
		FRHIResource* Resource = nullptr;   // FRHIBuffer* or FRHITexture*
		std::uint64_t Size = 0;
		std::uint32_t LastPass = 0;
		bool bBuffer = true;
		bool bFree = true;
	};

	std::vector<FSlot> Slots;

	FRHIBuffer* FindOrCreateBufferSlot(IRHI* RHI,
	                                    const FRHIBufferDesc& Desc,
	                                    std::uint32_t FirstUse,
	                                    std::uint32_t LastUse);
	FRHITexture* FindOrCreateTextureSlot(IRHI* RHI,
	                                      const FRHITextureDesc& Desc,
	                                      std::uint32_t FirstUse,
	                                      std::uint32_t LastUse);
};

} // namespace Maho
