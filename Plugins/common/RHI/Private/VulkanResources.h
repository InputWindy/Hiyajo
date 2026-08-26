#pragma once

#include <RHI/RHIResources.h>

#include "VulkanMemory.h"

#include <vulkan/vulkan.h>

namespace Maho
{

class FVulkanBuffer final : public FRHIBuffer
{
public:
	FVulkanBuffer(FRHIBufferDesc InDesc, VkBuffer InBuffer, VmaAllocation InAllocation,
		FVulkanMemoryAllocator* InAllocator, std::uint64_t InDeviceAddress = 0)
		: Desc(InDesc)
		, Buffer(InBuffer)
		, Allocation(InAllocation)
		, Allocator(InAllocator)
		, DeviceAddress(InDeviceAddress)
	{
	}

	~FVulkanBuffer() override;

	[[nodiscard]] const FRHIBufferDesc& GetDesc() const override
	{
		return Desc;
	}
	[[nodiscard]] std::uint64_t GetDeviceAddress() const override
	{
		return DeviceAddress;
	}
	[[nodiscard]] VkBuffer GetVkBuffer() const
	{
		return Buffer;
	}
	[[nodiscard]] VmaAllocation GetAllocation() const
	{
		return Allocation;
	}

private:
	friend class FVulkanStructuredBuffer;

	FRHIBufferDesc Desc{};
	VkBuffer Buffer = VK_NULL_HANDLE;
	VmaAllocation Allocation = nullptr;
	FVulkanMemoryAllocator* Allocator = nullptr;
	std::uint64_t DeviceAddress = 0;
};

class FVulkanStructuredBuffer final : public FRHIStructuredBuffer
{
public:
	FVulkanStructuredBuffer(FRHIStructuredBufferDesc InDesc, FVulkanBuffer* InUnderlying)
		: Desc(InDesc)
		, UnderlyingBuffer(InUnderlying)
	{
	}

	~FVulkanStructuredBuffer() override;

	[[nodiscard]] const FRHIStructuredBufferDesc& GetDesc() const override
	{
		return Desc;
	}
	[[nodiscard]] FRHIBuffer* GetUnderlyingBuffer() override
	{
		return UnderlyingBuffer;
	}
	[[nodiscard]] VkBuffer GetVkBuffer() const
	{
		return UnderlyingBuffer ? UnderlyingBuffer->GetVkBuffer() : VK_NULL_HANDLE;
	}

private:
	FRHIStructuredBufferDesc Desc{};
	FVulkanBuffer* UnderlyingBuffer = nullptr;
};

class FVulkanBufferView final : public FRHIBufferView
{
public:
	FVulkanBufferView(VkDevice InDevice, VkBufferView InView)
		: Device(InDevice)
		, View(InView)
	{
	}

	~FVulkanBufferView() override;

	[[nodiscard]] VkBufferView GetVkBufferView() const
	{
		return View;
	}

private:
	VkDevice Device = VK_NULL_HANDLE;
	VkBufferView View = VK_NULL_HANDLE;
};

class FVulkanTexture final : public FRHITexture
{
public:
	FVulkanTexture(FRHITextureDesc InDesc, VkImage InImage, VmaAllocation InAllocation, FVulkanMemoryAllocator* InAllocator)
		: Desc(InDesc)
		, Image(InImage)
		, Allocation(InAllocation)
		, Allocator(InAllocator)
	{
	}

	~FVulkanTexture() override;

	[[nodiscard]] const FRHITextureDesc& GetDesc() const override
	{
		return Desc;
	}
	[[nodiscard]] VkImage GetVkImage() const
	{
		return Image;
	}

private:
	FRHITextureDesc Desc{};
	VkImage Image = VK_NULL_HANDLE;
	VmaAllocation Allocation = nullptr;
	FVulkanMemoryAllocator* Allocator = nullptr;
};

class FVulkanSampler final : public FRHISampler
{
public:
	explicit FVulkanSampler(FRHISamplerDesc InDesc, VkDevice InDevice, VkSampler InSampler)
		: Desc(InDesc)
		, Device(InDevice)
		, Sampler(InSampler)
	{
	}

	~FVulkanSampler() override;

	[[nodiscard]] const FRHISamplerDesc& GetDesc() const override
	{
		return Desc;
	}
	[[nodiscard]] VkSampler GetVkSampler() const
	{
		return Sampler;
	}

private:
	FRHISamplerDesc Desc{};
	VkDevice Device = VK_NULL_HANDLE;
	VkSampler Sampler = VK_NULL_HANDLE;
};

class FVulkanShaderModule final : public FRHIShaderModule
{
public:
	FVulkanShaderModule(VkDevice InDevice, VkShaderModule InModule)
		: Device(InDevice)
		, Module(InModule)
	{
	}

	~FVulkanShaderModule() override;

	[[nodiscard]] VkShaderModule GetVkShaderModule() const
	{
		return Module;
	}

private:
	VkDevice Device = VK_NULL_HANDLE;
	VkShaderModule Module = VK_NULL_HANDLE;
};

class FVulkanGraphicsPipeline final : public FRHIGraphicsPipeline
{
public:
	FVulkanGraphicsPipeline(VkDevice InDevice, VkPipeline InPipeline, VkPipelineLayout InLayout)
		: Device(InDevice)
		, Pipeline(InPipeline)
		, Layout(InLayout)
	{
	}

	~FVulkanGraphicsPipeline() override;

	[[nodiscard]] VkPipeline GetVkPipeline() const
	{
		return Pipeline;
	}
	[[nodiscard]] VkPipelineLayout GetVkPipelineLayout() const
	{
		return Layout;
	}

private:
	VkDevice Device = VK_NULL_HANDLE;
	VkPipeline Pipeline = VK_NULL_HANDLE;
	VkPipelineLayout Layout = VK_NULL_HANDLE;
};

class FVulkanComputePipeline final : public FRHIComputePipeline
{
public:
	FVulkanComputePipeline(VkDevice InDevice, VkPipeline InPipeline, VkPipelineLayout InLayout)
		: Device(InDevice)
		, Pipeline(InPipeline)
		, Layout(InLayout)
	{
	}

	~FVulkanComputePipeline() override;

	[[nodiscard]] VkPipeline GetVkPipeline() const
	{
		return Pipeline;
	}
	[[nodiscard]] VkPipelineLayout GetVkPipelineLayout() const
	{
		return Layout;
	}

private:
	VkDevice Device = VK_NULL_HANDLE;
	VkPipeline Pipeline = VK_NULL_HANDLE;
	VkPipelineLayout Layout = VK_NULL_HANDLE;
};

class FVulkanFence final : public FRHIFence
{
public:
	FVulkanFence(VkDevice InDevice, VkFence InFence)
		: Device(InDevice)
		, Fence(InFence)
	{
	}

	~FVulkanFence() override;

	[[nodiscard]] VkFence GetVkFence() const
	{
		return Fence;
	}

private:
	VkDevice Device = VK_NULL_HANDLE;
	VkFence Fence = VK_NULL_HANDLE;
};

class FVulkanSemaphore final : public FRHISemaphore
{
public:
	FVulkanSemaphore(VkDevice InDevice, VkSemaphore InSemaphore)
		: Device(InDevice)
		, Semaphore(InSemaphore)
	{
	}

	~FVulkanSemaphore() override;

	[[nodiscard]] VkSemaphore GetVkSemaphore() const
	{
		return Semaphore;
	}

private:
	VkDevice Device = VK_NULL_HANDLE;
	VkSemaphore Semaphore = VK_NULL_HANDLE;
};

class FVulkanQueryPool final : public FRHIQueryPool
{
public:
	FVulkanQueryPool(VkDevice InDevice, VkQueryPool InPool)
		: Device(InDevice)
		, Pool(InPool)
	{
	}

	~FVulkanQueryPool() override;

	[[nodiscard]] VkQueryPool GetVkQueryPool() const
	{
		return Pool;
	}

private:
	VkDevice Device = VK_NULL_HANDLE;
	VkQueryPool Pool = VK_NULL_HANDLE;
};

class FVulkanAccelerationStructure final : public FRHIAccelerationStructure
{
public:
	FVulkanAccelerationStructure(
		VkDevice InDevice,
		VkAccelerationStructureKHR InAccel,
		VkBuffer InStorageBuffer,
		VmaAllocation InAllocation,
		FVulkanMemoryAllocator* InAllocator,
		std::uint64_t InDeviceAddress,
		FRHIRayTracingGeometryDesc InGeometryDesc)
		: Device(InDevice)
		, Accel(InAccel)
		, StorageBuffer(InStorageBuffer)
		, Allocation(InAllocation)
		, Allocator(InAllocator)
		, DeviceAddress(InDeviceAddress)
	{
		GeometryDesc = std::move(InGeometryDesc);
	}

	~FVulkanAccelerationStructure() override;

	[[nodiscard]] VkAccelerationStructureKHR GetVkAccelerationStructure() const
	{
		return Accel;
	}
	[[nodiscard]] VkBuffer GetVkStorageBuffer() const
	{
		return StorageBuffer;
	}
	[[nodiscard]] std::uint64_t GetDeviceAddress() const
	{
		return DeviceAddress;
	}

private:
	VkDevice Device = VK_NULL_HANDLE;
	VkAccelerationStructureKHR Accel = VK_NULL_HANDLE;
	VkBuffer StorageBuffer = VK_NULL_HANDLE;
	VmaAllocation Allocation = nullptr;
	FVulkanMemoryAllocator* Allocator = nullptr;
	std::uint64_t DeviceAddress = 0;
};

class FVulkanRayTracingPipeline final : public FRHIRayTracingPipeline
{
public:
	FVulkanRayTracingPipeline(VkDevice InDevice, VkPipeline InPipeline, VkPipelineLayout InLayout)
		: Device(InDevice)
		, Pipeline(InPipeline)
		, Layout(InLayout)
	{
	}

	~FVulkanRayTracingPipeline() override;

	[[nodiscard]] VkPipeline GetVkPipeline() const
	{
		return Pipeline;
	}
	[[nodiscard]] VkPipelineLayout GetVkPipelineLayout() const
	{
		return Layout;
	}

private:
	VkDevice Device = VK_NULL_HANDLE;
	VkPipeline Pipeline = VK_NULL_HANDLE;
	VkPipelineLayout Layout = VK_NULL_HANDLE;
};

class FVulkanTextureView final : public FRHITextureView
{
public:
	FVulkanTextureView(VkDevice InDevice, VkImageView InView)
		: Device(InDevice)
		, View(InView)
	{
	}

	~FVulkanTextureView() override;

	[[nodiscard]] VkImageView GetVkImageView() const
	{
		return View;
	}

private:
	VkDevice Device = VK_NULL_HANDLE;
	VkImageView View = VK_NULL_HANDLE;
};

class FVulkanDescriptorSetLayout final : public FRHIDescriptorSetLayout
{
public:
	FVulkanDescriptorSetLayout(VkDevice InDevice, VkDescriptorSetLayout InLayout)
		: Device(InDevice)
		, Layout(InLayout)
	{
	}

	~FVulkanDescriptorSetLayout() override;

	[[nodiscard]] VkDescriptorSetLayout GetVkLayout() const
	{
		return Layout;
	}

private:
	VkDevice Device = VK_NULL_HANDLE;
	VkDescriptorSetLayout Layout = VK_NULL_HANDLE;
};

class FVulkanPipelineLayout final : public FRHIPipelineLayout
{
public:
	FVulkanPipelineLayout(VkDevice InDevice, VkPipelineLayout InLayout)
		: Device(InDevice)
		, Layout(InLayout)
	{
	}

	~FVulkanPipelineLayout() override;

	[[nodiscard]] VkPipelineLayout GetVkLayout() const
	{
		return Layout;
	}

private:
	VkDevice Device = VK_NULL_HANDLE;
	VkPipelineLayout Layout = VK_NULL_HANDLE;
};

class FVulkanDescriptorPool final : public FRHIDescriptorPool
{
public:
	FVulkanDescriptorPool(VkDevice InDevice, VkDescriptorPool InPool)
		: Device(InDevice)
		, Pool(InPool)
	{
	}

	~FVulkanDescriptorPool() override;

	[[nodiscard]] VkDescriptorPool GetVkPool() const
	{
		return Pool;
	}

private:
	VkDevice Device = VK_NULL_HANDLE;
	VkDescriptorPool Pool = VK_NULL_HANDLE;
};

class FVulkanDescriptorSet final : public FRHIDescriptorSet
{
public:
	FVulkanDescriptorSet(VkDescriptorSet InSet)
		: Set(InSet)
	{
	}

	[[nodiscard]] VkDescriptorSet GetVkSet() const
	{
		return Set;
	}

private:
	VkDescriptorSet Set = VK_NULL_HANDLE;
};

class FVulkanRenderPass final : public FRHIRenderPass
{
public:
	FVulkanRenderPass(VkDevice InDevice, VkRenderPass InPass)
		: Device(InDevice)
		, Pass(InPass)
	{
	}

	~FVulkanRenderPass() override;

	[[nodiscard]] VkRenderPass GetVkPass() const
	{
		return Pass;
	}

private:
	VkDevice Device = VK_NULL_HANDLE;
	VkRenderPass Pass = VK_NULL_HANDLE;
};

class FVulkanFramebuffer final : public FRHIFramebuffer
{
public:
	FVulkanFramebuffer(VkDevice InDevice, VkFramebuffer InFB)
		: Device(InDevice)
		, FB(InFB)
	{
	}

	~FVulkanFramebuffer() override;

	[[nodiscard]] VkFramebuffer GetVkFramebuffer() const
	{
		return FB;
	}

private:
	VkDevice Device = VK_NULL_HANDLE;
	VkFramebuffer FB = VK_NULL_HANDLE;
};

} // namespace Maho
