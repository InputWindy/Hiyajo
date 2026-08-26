#pragma once

#include <RHI/RHIResources.h>

#include <vulkan/vulkan.h>

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>

namespace Maho
{

class FVulkanMemoryAllocator final : public IRHIMemoryAllocator
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

	[[nodiscard]] static VmaAllocationCreateInfo MakeAllocationInfo(ERHIMemoryUsage MemoryUsage);

	virtual void Free(FRHIMemoryAllocation& Alloc) override;
	virtual void* Map(FRHIMemoryAllocation& Alloc) override;
	virtual void Unmap(FRHIMemoryAllocation& Alloc) override;

private:
	VmaAllocator Allocator = nullptr;
};

} // namespace Maho
