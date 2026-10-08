#pragma once

#include <RHI/RHIResources.h>

#include "VulkanMemory.h"

#include <algorithm>
#include <vulkan/vulkan.h>

namespace Maho
{

/**
 * The concrete texel extent a copy region covers at its own mip level. A region Extent of
 * {0,0,0} means "the whole mip level", so the level's size is derived from the texture desc;
 * for a 3D texture the level's depth IS the slice count (Vulkan wants layerCount 1 for those).
 * Shared by the backend's copy path and its readback, so the two cannot disagree about how
 * many bytes a region is.
 */
[[nodiscard]] inline FRHIExtent3D ResolveCopyExtent(
	const FRHITextureDesc& Desc, const FRHITextureCopyRegion& Region)
{
	const std::uint32_t Mip = Region.MipLevel;
	FRHIExtent3D Extent;
	Extent.Width = (std::max)(1u, Desc.Extent.Width >> Mip);
	Extent.Height = (std::max)(1u, Desc.Extent.Height >> Mip);
	Extent.Depth = (std::max)(1u, Desc.Extent.Depth >> Mip);

	if (Region.Extent.Width != 0)
	{
		Extent.Width = Region.Extent.Width;
	}
	if (Region.Extent.Height != 0)
	{
		Extent.Height = Region.Extent.Height;
	}
	if (Region.Extent.Depth != 0)
	{
		Extent.Depth = Region.Extent.Depth;
	}
	return Extent;
}

/** Vulkan image-subresource layerCount for a region: a 3D image addresses its slices through
 *  the copy extent, so it always declares ONE layer. */
[[nodiscard]] inline std::uint32_t ResolveCopyLayerCount(
	const FRHITextureDesc& Desc, const FRHITextureCopyRegion& Region)
{
	return Desc.Dimension == ERHITextureDimension::Tex3D ? 1u : Region.LayerCount;
}

/** Image view aspect from an ERHIFormat: depth(+stencil) formats are NOT color. */
[[nodiscard]] inline VkImageAspectFlags GetVulkanAspectMask(ERHIFormat Format)
{
	switch (Format)
	{
	case ERHIFormat::D24_UNORM_S8_UINT:
		return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	case ERHIFormat::D32_SFLOAT:
		return VK_IMAGE_ASPECT_DEPTH_BIT;
	default:
		return VK_IMAGE_ASPECT_COLOR_BIT;
	}
}

/**
 * The Vulkan state a texture is in: what the STATE MACHINE remembers between commands.
 *
 * A layout alone is not enough to record the NEXT barrier correctly -- the barrier's source stage
 * and source access must be the ones that produced the current contents, which is why they travel
 * with the layout instead of being guessed at the use site (guessing is what "oldLayout =
 * COLOR_ATTACHMENT_OPTIMAL because we rendered into it last frame" was).
 *
 * The default is the initialized state: layout undefined (contents must not be assumed), no producer
 * to wait for, no access to make visible. That is exactly what a freshly created or freshly acquired
 * image is, so `FVulkanImageState{}` is also the RESET value.
 */
struct FVulkanImageState
{
	VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkPipelineStageFlags Stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	VkAccessFlags Access = 0;
};

/**
 * Usage -> (layout, stage, access). THE table: one place decides what a purpose means in Vulkan, so
 * the deduction layer, BeginRendering, the copies and the present path cannot disagree.
 *
 * Stages are chosen to cover every stage the purpose can be consumed in (a texture sampled by a
 * vertex shader is the case a fragment-only mask gets wrong), and the access mask is the access that
 * purpose performs, which is also what the NEXT transition uses as its source access.
 */
[[nodiscard]] inline FVulkanImageState ToVulkanImageState(ERHIResourceUsage Usage)
{
	switch (Usage)
	{
	case ERHIResourceUsage::ColorAttachment:
		return FVulkanImageState{ VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT };
	case ERHIResourceUsage::DepthAttachment:
		// Read | write: a depth test READS the attachment even when the pass never writes it.
		return FVulkanImageState{ VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT };
	case ERHIResourceUsage::Sampled:
	case ERHIResourceUsage::UniformRead:
		return FVulkanImageState{ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
				| VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_ACCESS_SHADER_READ_BIT };
	case ERHIResourceUsage::TransferSrc:
		return FVulkanImageState{ VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT };
	case ERHIResourceUsage::TransferDst:
		return FVulkanImageState{ VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT };
	case ERHIResourceUsage::Present:
	default:
		return FVulkanImageState{ VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0 };
	}
}

/**
 * Legacy ERHIResourceState -> purpose, so the hand-written `TransitionTexture(Old, New)` call sites
 * feed the SAME state machine instead of bypassing it (and so their NewState can no longer disagree
 * with what the RHI tracks). Returns false for states that name no image layout at all -- Common
 * ("no layout") and the buffer states -- which is the caller's "nothing to do".
 */
[[nodiscard]] inline bool ToResourceUsage(ERHIResourceState State, ERHIResourceUsage& OutUsage)
{
	switch (State)
	{
	case ERHIResourceState::RenderTarget:   OutUsage = ERHIResourceUsage::ColorAttachment; return true;
	case ERHIResourceState::DepthWrite:     OutUsage = ERHIResourceUsage::DepthAttachment; return true;
	case ERHIResourceState::ShaderResource: OutUsage = ERHIResourceUsage::Sampled;         return true;
	case ERHIResourceState::CopySrc:        OutUsage = ERHIResourceUsage::TransferSrc;     return true;
	case ERHIResourceState::CopyDst:        OutUsage = ERHIResourceUsage::TransferDst;     return true;
	case ERHIResourceState::Present:        OutUsage = ERHIResourceUsage::Present;         return true;
	default:
		return false;
	}
}

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

	// -- tracked state (the RHI's per-texture layout bookkeeping) --
	//
	// Created as FVulkanImageState{} -- layout undefined, contents not to be assumed -- and advanced
	// ONLY by the state machine (FVulkanCommandList::ApplyUsage / FVulkanRHI::TransitionTrackedImage).
	// No lock: the barrier decisions for a frame are made on the recording threads, and (per the
	// engine's own model) the frames of one collector advance one after another, so the writers are
	// serialized by the graph rather than by a mutex here. There is exactly ONE tracked layout per
	// texture, not one per mip/layer: a partially transitioned subresource therefore advances the
	// whole-texture state -- correct for the full-range uses this engine records, and deliberately
	// coarse until a pass needs per-subresource tracking.
	[[nodiscard]] const FVulkanImageState& GetTrackedState() const
	{
		return Tracked;
	}
	[[nodiscard]] FVulkanImageState& TrackedStateRef()
	{
		return Tracked;
	}
	/** Back to "contents not defined" (a recreated image, or a re-acquired swapchain image). */
	void ResetTrackedState()
	{
		Tracked = FVulkanImageState{};
		ReportedContentLoss = false;
	}
	/** Once-per-texture latch for the "this transition just discarded the content you are about to
	 *  load" diagnostic -- a per-frame report would be noise, silence would hide a lost frame. */
	bool ReportedContentLoss = false;

private:
	FRHITextureDesc Desc{};
	VkImage Image = VK_NULL_HANDLE;
	VmaAllocation Allocation = nullptr;
	FVulkanMemoryAllocator* Allocator = nullptr;
	FVulkanImageState Tracked{};
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
	FVulkanTextureView(VkDevice InDevice, VkImageView InView, FRHITexture* InSourceTexture)
		: Device(InDevice)
		, View(InView)
		, SourceTexture(InSourceTexture)
	{
	}

	~FVulkanTextureView() override;

	[[nodiscard]] VkImageView GetVkImageView() const
	{
		return View;
	}
	/** The texture this view looks at. BeginRendering needs it: an attachment arrives as a VIEW, but
	 *  the state machine tracks TEXTURES (the barrier targets the image, not the view). */
	[[nodiscard]] FRHITexture* GetSourceTexture() const
	{
		return SourceTexture;
	}

private:
	VkDevice Device = VK_NULL_HANDLE;
	VkImageView View = VK_NULL_HANDLE;
	FRHITexture* SourceTexture = nullptr;
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
	FVulkanRenderPass(VkDevice InDevice, VkRenderPass InPass, bool bInOwnsHandle = true)
		: Device(InDevice)
		, Pass(InPass)
		, bOwnsHandle(bInOwnsHandle)
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
	bool bOwnsHandle = true;   // false for the swapchain render pass (swapchain frees it)
};

class FVulkanFramebuffer final : public FRHIFramebuffer
{
public:
	FVulkanFramebuffer(VkDevice InDevice, VkFramebuffer InFB, bool bInOwnsHandle = true)
		: Device(InDevice)
		, FB(InFB)
		, bOwnsHandle(bInOwnsHandle)
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
	bool bOwnsHandle = true;   // false for swapchain framebuffers (swapchain frees them)
};

} // namespace Maho
