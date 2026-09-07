#pragma once

#include <RHI/RHIResources.h>

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

	/** Queue a staging buffer for destruction. It is kept alive until
	 *  FlushDeferredFrees() -- called at the NEXT frame boundary, AFTER the host
	 *  waited the previous frame's fence -- so a recorded vkCmdCopyBuffer still
	 *  executing on the GPU never reads a freed staging allocation. This is the
	 *  async-upload lifetime rule: a host-visible staging that a recorded command
	 *  writes + copies stays alive until the frame referencing it is retired. */
	void DestroyBufferDeferred(VkBuffer Buffer, VmaAllocation Allocation);

	/** Destroy every buffer queued by DestroyBufferDeferred since the last flush.
	 *  MUST run at a frame boundary AFTER the previous frame's fence is signaled
	 *  (so all their copies are complete). */
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

	VmaAllocator Allocator = nullptr;
	std::mutex DeferredMutex;                 // guards DeferredBuffers (recording threads call DestroyBufferDeferred)
	std::vector<FDeferredBuffer> DeferredBuffers;   // retried staging queued this frame; freed at the next frame boundary
};

} // namespace Maho
