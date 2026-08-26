#include "VulkanResources.h"

namespace Maho
{

FVulkanBuffer::~FVulkanBuffer()
{
	if (Allocator != nullptr && Buffer != VK_NULL_HANDLE)
	{
		Allocator->DestroyBuffer(Buffer, Allocation);
		Buffer = VK_NULL_HANDLE;
		Allocation = nullptr;
	}
}

FVulkanStructuredBuffer::~FVulkanStructuredBuffer()
{
	// Underlying buffer is owned by the creator; do not destroy it here.
	UnderlyingBuffer = nullptr;
}

FVulkanBufferView::~FVulkanBufferView()
{
	if (Device != VK_NULL_HANDLE && View != VK_NULL_HANDLE)
	{
		vkDestroyBufferView(Device, View, nullptr);
		View = VK_NULL_HANDLE;
	}
}

FVulkanTexture::~FVulkanTexture()
{
	if (Allocator != nullptr && Image != VK_NULL_HANDLE)
	{
		Allocator->DestroyImage(Image, Allocation);
		Image = VK_NULL_HANDLE;
		Allocation = nullptr;
	}
}

FVulkanSampler::~FVulkanSampler()
{
	if (Device != VK_NULL_HANDLE && Sampler != VK_NULL_HANDLE)
	{
		vkDestroySampler(Device, Sampler, nullptr);
		Sampler = VK_NULL_HANDLE;
	}
}

FVulkanShaderModule::~FVulkanShaderModule()
{
	if (Device != VK_NULL_HANDLE && Module != VK_NULL_HANDLE)
	{
		vkDestroyShaderModule(Device, Module, nullptr);
		Module = VK_NULL_HANDLE;
	}
}

FVulkanGraphicsPipeline::~FVulkanGraphicsPipeline()
{
	if (Device != VK_NULL_HANDLE && Pipeline != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(Device, Pipeline, nullptr);
		Pipeline = VK_NULL_HANDLE;
	}
}

FVulkanComputePipeline::~FVulkanComputePipeline()
{
	if (Device != VK_NULL_HANDLE && Pipeline != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(Device, Pipeline, nullptr);
		Pipeline = VK_NULL_HANDLE;
	}
}

FVulkanFence::~FVulkanFence()
{
	if (Device != VK_NULL_HANDLE && Fence != VK_NULL_HANDLE)
	{
		vkDestroyFence(Device, Fence, nullptr);
		Fence = VK_NULL_HANDLE;
	}
}

FVulkanSemaphore::~FVulkanSemaphore()
{
	if (Device != VK_NULL_HANDLE && Semaphore != VK_NULL_HANDLE)
	{
		vkDestroySemaphore(Device, Semaphore, nullptr);
		Semaphore = VK_NULL_HANDLE;
	}
}

FVulkanQueryPool::~FVulkanQueryPool()
{
	if (Device != VK_NULL_HANDLE && Pool != VK_NULL_HANDLE)
	{
		vkDestroyQueryPool(Device, Pool, nullptr);
		Pool = VK_NULL_HANDLE;
	}
}

FVulkanTextureView::~FVulkanTextureView()
{
	if (Device != VK_NULL_HANDLE && View != VK_NULL_HANDLE)
	{
		vkDestroyImageView(Device, View, nullptr);
		View = VK_NULL_HANDLE;
	}
}

FVulkanDescriptorSetLayout::~FVulkanDescriptorSetLayout()
{
	if (Device != VK_NULL_HANDLE && Layout != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorSetLayout(Device, Layout, nullptr);
		Layout = VK_NULL_HANDLE;
	}
}

FVulkanPipelineLayout::~FVulkanPipelineLayout()
{
	if (Device != VK_NULL_HANDLE && Layout != VK_NULL_HANDLE)
	{
		vkDestroyPipelineLayout(Device, Layout, nullptr);
		Layout = VK_NULL_HANDLE;
	}
}

FVulkanDescriptorPool::~FVulkanDescriptorPool()
{
	if (Device != VK_NULL_HANDLE && Pool != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorPool(Device, Pool, nullptr);
		Pool = VK_NULL_HANDLE;
	}
}

FVulkanRenderPass::~FVulkanRenderPass()
{
	if (Device != VK_NULL_HANDLE && Pass != VK_NULL_HANDLE)
	{
		vkDestroyRenderPass(Device, Pass, nullptr);
		Pass = VK_NULL_HANDLE;
	}
}

FVulkanFramebuffer::~FVulkanFramebuffer()
{
	if (Device != VK_NULL_HANDLE && FB != VK_NULL_HANDLE)
	{
		vkDestroyFramebuffer(Device, FB, nullptr);
		FB = VK_NULL_HANDLE;
	}
}

FVulkanAccelerationStructure::~FVulkanAccelerationStructure()
{
	// KHR functions are not guaranteed in the static Vulkan loader — resolve
	// per-device like the rest of the dynamic Vulkan usage in this plugin.
	if (Device != VK_NULL_HANDLE && Accel != VK_NULL_HANDLE)
	{
		auto DestroyFn = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
			vkGetDeviceProcAddr(Device, "vkDestroyAccelerationStructureKHR"));
		if (DestroyFn)
		{
			DestroyFn(Device, Accel, nullptr);
		}
		Accel = VK_NULL_HANDLE;
	}
	if (Allocator != nullptr && StorageBuffer != VK_NULL_HANDLE)
	{
		Allocator->DestroyBuffer(StorageBuffer, Allocation);
		StorageBuffer = VK_NULL_HANDLE;
		Allocation = nullptr;
	}
}

FVulkanRayTracingPipeline::~FVulkanRayTracingPipeline()
{
	if (Device != VK_NULL_HANDLE && Pipeline != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(Device, Pipeline, nullptr);
		Pipeline = VK_NULL_HANDLE;
	}
}

} // namespace Maho
