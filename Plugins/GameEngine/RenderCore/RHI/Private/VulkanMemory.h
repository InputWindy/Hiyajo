#pragma once

#include <RHI/RHIResources.h>

#include <Core/FrameGraph.h>   // MAHO_FRAMES_IN_FLIGHT: the frame slots the deferred staging is bucketed by

#include <array>
#include <cstdint>
#include <mutex>
#include <vector>

#include <vulkan/vulkan.h>

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>

namespace Maho
{

class FVulkanMemoryAllocator final : public IDynamicRHIMemoryAllocator
{
public:
	FVulkanMemoryAllocator() = default;
	~FVulkanMemoryAllocator() override;

	bool Initialize(VkInstance Instance, VkPhysicalDevice PhysicalDevice, VkDevice Device);
	void Shutdown();

	[[nodiscard]] VmaAllocator GetAllocator() const
	{
		return Allocator;
	}
	[[nodiscard]] bool IsValid() const
	{
		return Allocator != nullptr;
	}

	[[nodiscard]] bool CreateBuffer(
		const VkBufferCreateInfo& BufferInfo,
		const VmaAllocationCreateInfo& AllocInfo,
		VkBuffer& OutBuffer,
		VmaAllocation& OutAllocation,
		FRHIMemoryAllocation* OutOpaque = nullptr);

	[[nodiscard]] bool CreateImage(
		const VkImageCreateInfo& ImageInfo,
		const VmaAllocationCreateInfo& AllocInfo,
		VkImage& OutImage,
		VmaAllocation& OutAllocation,
		FRHIMemoryAllocation* OutOpaque = nullptr);

	void DestroyBuffer(VkBuffer Buffer, VmaAllocation Allocation);
	void DestroyImage(VkImage Image, VmaAllocation Allocation);

	/** Queue a staging buffer for destruction. It is kept alive until the frame that recorded its
	 *  `vkCmdCopyBuffer` has RETIRED -- the RHI frame head waits the per-slot fence of the frame
	 *  `MAHO_FRAMES_IN_FLIGHT` slots back (`RetireDeferredForSlot` below), so the staging is bucketed
	 *  by the frame SLOT that recorded it and freed when that slot comes round again. Freeing it at
	 *  "the next frame boundary" was true only while the device held a single fence (depth 1): at
	 *  depth K it hands the queue a staging buffer the GPU is still reading, reported as
	 *  `VUID-vkDestroyBuffer-buffer-00922` ("is currently in use by VkCommandBuffer").
	 *  This is the async-upload lifetime rule: a host-visible staging that a recorded command writes +
	 *  copies stays alive until the frame referencing it is retired. */
	void DestroyBufferDeferred(VkBuffer Buffer, VmaAllocation Allocation);

	/** The frame head's bookkeeping end: `Slot` is the frame slot whose fence was JUST waited, so its
	 *  bucket of deferred staging is provably complete and is freed now; every staging deferred from
	 *  here on belongs to `Slot` and is freed the next time this slot's turn comes round. Called once
	 *  per frame from `FVulkanRHI::BeginFrame` with the slot it opened. */
	void RetireDeferredForSlot(std::uint32_t Slot);

	/** Destroy every buffer queued by DestroyBufferDeferred, whatever slot it belongs to.
	 *  Only legal while the device is idle (Shutdown): it does not wait any fence. */
	void FlushDeferredFrees();

	[[nodiscard]] static VmaAllocationCreateInfo MakeAllocationInfo(ERHIMemoryUsage MemoryUsage);

	virtual void Free(FRHIMemoryAllocation& Alloc) override;
	virtual void* Map(FRHIMemoryAllocation& Alloc) override;
	virtual void Unmap(FRHIMemoryAllocation& Alloc) override;

private:
	struct FDeferredBuffer
	{
		VkBuffer Buffer = VK_NULL_HANDLE;
		VmaAllocation Allocation = nullptr;
	};

	/** Frame slots, one bucket per frame the device lets overlap (see FVulkanRHI::kFrameSlots). */
	static constexpr std::uint32_t kFrameSlots = MAHO_FRAMES_IN_FLIGHT;

	VmaAllocator Allocator = nullptr;
	std::mutex DeferredMutex;                 // guards the buckets + CurrentSlot (recording threads defer)
	std::array<std::vector<FDeferredBuffer>, kFrameSlots> DeferredBuffers{};   // staging queued per frame slot
	std::uint32_t CurrentSlot = 0;            // the slot new defers are tagged with (set by the frame head)
};

} // namespace Maho
