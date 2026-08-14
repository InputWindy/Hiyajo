#define VMA_IMPLEMENTATION
#include "VulkanMemory.h"

#include <Core/Misc/Log.h>

namespace Maho
{

FVulkanMemoryAllocator::~FVulkanMemoryAllocator()
{
	Shutdown();
}

bool FVulkanMemoryAllocator::Initialize(VkInstance Instance, VkPhysicalDevice PhysicalDevice, VkDevice Device)
{
	if (Allocator != nullptr)
	{
		return true;
	}

	VmaVulkanFunctions VulkanFunctions{};
	VulkanFunctions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
	VulkanFunctions.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;

	VmaAllocatorCreateInfo CreateInfo{};
	CreateInfo.flags = VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT;
	CreateInfo.physicalDevice = PhysicalDevice;
	CreateInfo.device = Device;
	CreateInfo.instance = Instance;
	CreateInfo.pVulkanFunctions = &VulkanFunctions;

	const VkResult Result = vmaCreateAllocator(&CreateInfo, &Allocator);
	if (Result != VK_SUCCESS)
	{
		MAHO_CORE_ERROR("FVulkanMemoryAllocator::Initialize: vmaCreateAllocator failed ({})", static_cast<int>(Result));
		Allocator = nullptr;
		return false;
	}

	MAHO_CORE_INFO("VMA allocator created");
	return true;
}

void FVulkanMemoryAllocator::Shutdown()
{
	if (Allocator != nullptr)
	{
		vmaDestroyAllocator(Allocator);
		Allocator = nullptr;
	}
}

VmaAllocationCreateInfo FVulkanMemoryAllocator::MakeAllocationInfo(ERHIMemoryUsage MemoryUsage)
{
	VmaAllocationCreateInfo Info{};
	Info.usage = VMA_MEMORY_USAGE_AUTO;

	switch (MemoryUsage)
	{
	case ERHIMemoryUsage::GPUOnly:
		Info.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		break;
	case ERHIMemoryUsage::CPUToGPU:
		Info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
			| VMA_ALLOCATION_CREATE_MAPPED_BIT;
		break;
	case ERHIMemoryUsage::GPUToCPU:
		Info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
			| VMA_ALLOCATION_CREATE_MAPPED_BIT;
		break;
	case ERHIMemoryUsage::CPUOnly:
		Info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
			| VMA_ALLOCATION_CREATE_MAPPED_BIT;
		Info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		break;
	}

	return Info;
}

bool FVulkanMemoryAllocator::CreateBuffer(
	const VkBufferCreateInfo& BufferInfo,
	const VmaAllocationCreateInfo& AllocInfo,
	VkBuffer& OutBuffer,
	VmaAllocation& OutAllocation,
	FRHIMemoryAllocation* OutOpaque)
{
	OutBuffer = VK_NULL_HANDLE;
	OutAllocation = nullptr;

	if (Allocator == nullptr)
	{
		return false;
	}

	VmaAllocationInfo AllocResult{};
	const VkResult Result = vmaCreateBuffer(Allocator, &BufferInfo, &AllocInfo, &OutBuffer, &OutAllocation, &AllocResult);
	if (Result != VK_SUCCESS)
	{
		MAHO_CORE_ERROR("FVulkanMemoryAllocator::CreateBuffer failed ({})", static_cast<int>(Result));
		return false;
	}

	if (OutOpaque != nullptr)
	{
		OutOpaque->Native = OutAllocation;
		OutOpaque->Mapped = AllocResult.pMappedData;
	}

	return true;
}

bool FVulkanMemoryAllocator::CreateImage(
	const VkImageCreateInfo& ImageInfo,
	const VmaAllocationCreateInfo& AllocInfo,
	VkImage& OutImage,
	VmaAllocation& OutAllocation,
	FRHIMemoryAllocation* OutOpaque)
{
	OutImage = VK_NULL_HANDLE;
	OutAllocation = nullptr;

	if (Allocator == nullptr)
	{
		return false;
	}

	VmaAllocationInfo AllocResult{};
	const VkResult Result = vmaCreateImage(Allocator, &ImageInfo, &AllocInfo, &OutImage, &OutAllocation, &AllocResult);
	if (Result != VK_SUCCESS)
	{
		MAHO_CORE_ERROR("FVulkanMemoryAllocator::CreateImage failed ({})", static_cast<int>(Result));
		return false;
	}

	if (OutOpaque != nullptr)
	{
		OutOpaque->Native = OutAllocation;
		OutOpaque->Mapped = AllocResult.pMappedData;
	}

	return true;
}

void FVulkanMemoryAllocator::DestroyBuffer(VkBuffer Buffer, VmaAllocation Allocation)
{
	if (Allocator != nullptr && Buffer != VK_NULL_HANDLE)
	{
		vmaDestroyBuffer(Allocator, Buffer, Allocation);
	}
}

void FVulkanMemoryAllocator::DestroyImage(VkImage Image, VmaAllocation Allocation)
{
	if (Allocator != nullptr && Image != VK_NULL_HANDLE)
	{
		vmaDestroyImage(Allocator, Image, Allocation);
	}
}

void FVulkanMemoryAllocator::Free(FRHIMemoryAllocation& Alloc)
{
	Alloc.Native = nullptr;
	Alloc.Mapped = nullptr;
}

void* FVulkanMemoryAllocator::Map(FRHIMemoryAllocation& Alloc)
{
	if (Allocator == nullptr || Alloc.Native == nullptr)
	{
		return nullptr;
	}

	if (Alloc.Mapped != nullptr)
	{
		return Alloc.Mapped;
	}

	void* Mapped = nullptr;
	if (vmaMapMemory(Allocator, static_cast<VmaAllocation>(Alloc.Native), &Mapped) != VK_SUCCESS)
	{
		return nullptr;
	}

	Alloc.Mapped = Mapped;
	return Mapped;
}

void FVulkanMemoryAllocator::Unmap(FRHIMemoryAllocation& Alloc)
{
	if (Allocator == nullptr || Alloc.Native == nullptr)
	{
		return;
	}

	vmaUnmapMemory(Allocator, static_cast<VmaAllocation>(Alloc.Native));
	Alloc.Mapped = nullptr;
}

} // namespace Maho
