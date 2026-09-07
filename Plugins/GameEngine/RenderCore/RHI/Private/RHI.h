#pragma once

#include <RHI/RHIAPI.h>
#include <RHI/RHICommandList.h>
#include <RHI/RHIEnums.h>
#include <RHI/RHIResources.h>

#include <cstdint>
#include <limits>
#include <memory>

namespace Maho
{

struct FRHIInitDesc
{
	ERHIBackend Backend = ERHIBackend::Vulkan;

	/** Native window handle for WSI (HWND on Win32). */
	void* NativeWindowHandle = nullptr;

	int FramebufferWidth = 0;
	int FramebufferHeight = 0;
};

/**
 * Render-hardware interface. Public surface has no Vulkan / VMA types.
 *
 * Device creates resources via internal factories.
 * Always exposes Graphics / Compute / Transfer logical queues.
 */
class MAHO_RHI_API IDynamicRHI
{
public:
	virtual ~IDynamicRHI() = default;

	IDynamicRHI(const IDynamicRHI&) = delete;
	IDynamicRHI& operator=(const IDynamicRHI&) = delete;

	virtual bool Initialize(const FRHIInitDesc& Desc) = 0;
	virtual void Shutdown() = 0;

	virtual void BeginFrame() = 0;
	virtual void EndFrame() = 0;
	virtual void Resize(int Width, int Height) = 0;
	virtual void WaitIdle() = 0;

	/**
	 * Borrow the frame command buffer (already begun by BeginFrame, ended and
	 * submitted by EndFrame) as a non-owning recording surface.
	 */
	[[nodiscard]] virtual FRHICommandList* GetFrameCommandList() = 0;

	/** Blit an off-screen texture to the current swapchain backbuffer. */
	virtual void PresentTexture(FRHITexture* Src) = 0;

	[[nodiscard]] virtual ERHIFormat GetSwapchainFormat() const = 0;

	[[nodiscard]] virtual bool IsInitialized() const = 0;

	[[nodiscard]] virtual IDynamicRHIMemoryAllocator* GetMemoryAllocator() = 0;

	[[nodiscard]] virtual FRHIQueue& GetGraphicsQueue() = 0;
	[[nodiscard]] virtual FRHIQueue& GetComputeQueue() = 0;
	[[nodiscard]] virtual FRHIQueue& GetTransferQueue() = 0;

	[[nodiscard]] virtual FRHICommandList* CreateCommandList(ERHICommandListType Type) = 0;
	virtual void DestroyCommandList(FRHICommandList* CmdList) = 0;

	[[nodiscard]] virtual FRHIFence* CreateFence(bool bSignaled) = 0;
	virtual void DestroyFence(FRHIFence* Fence) = 0;
	virtual void WaitForFence(FRHIFence* Fence, std::uint64_t TimeoutNs = (std::numeric_limits<std::uint64_t>::max)()) = 0;
	/** Non-blocking fence query (vkGetFenceStatus). */
	[[nodiscard]] virtual bool IsFenceSignaled(FRHIFence* Fence) = 0;
	/** Reset a fence to unsignaled (before re-submitting work with it). */
	virtual void ResetFence(FRHIFence* Fence) = 0;

	[[nodiscard]] virtual FRHISemaphore* CreateGpuSemaphore() = 0;
	virtual void DestroyGpuSemaphore(FRHISemaphore* Semaphore) = 0;

	/** Internal resource factories. */
	[[nodiscard]] virtual FRHIBuffer* CreateBuffer(const FRHIBufferDesc& Desc) = 0;
	virtual void DestroyBuffer(FRHIBuffer* Buffer) = 0;
	[[nodiscard]] virtual FRHITexture* CreateTexture(const FRHITextureDesc& Desc) = 0;
	virtual void DestroyTexture(FRHITexture* Texture) = 0;
	[[nodiscard]] virtual FRHISampler* CreateSampler(const FRHISamplerDesc& Desc) = 0;
	virtual void DestroySampler(FRHISampler* Sampler) = 0;
	[[nodiscard]] virtual FRHIShaderModule* CreateShaderModule(const FRHIShaderModuleDesc& Desc) = 0;
	virtual void DestroyShaderModule(FRHIShaderModule* Module) = 0;
	[[nodiscard]] virtual FRHIGraphicsPipeline* CreateGraphicsPipeline(const FRHIGraphicsPipelineDesc& Desc) = 0;
	virtual void DestroyGraphicsPipeline(FRHIGraphicsPipeline* Pipeline) = 0;
	[[nodiscard]] virtual FRHIComputePipeline* CreateComputePipeline(const FRHIComputePipelineDesc& Desc) = 0;
	virtual void DestroyComputePipeline(FRHIComputePipeline* Pipeline) = 0;

	[[nodiscard]] virtual FRHIStructuredBuffer* CreateStructuredBuffer(const FRHIStructuredBufferDesc& Desc) = 0;
	virtual void DestroyStructuredBuffer(FRHIStructuredBuffer* Buffer) = 0;
	[[nodiscard]] virtual FRHIBufferView* CreateBufferView(const FRHIBufferViewDesc& Desc) = 0;
	virtual void DestroyBufferView(FRHIBufferView* View) = 0;

	[[nodiscard]] virtual FRHITextureView* CreateTextureView(const FRHITextureViewDesc& Desc) = 0;
	virtual void DestroyTextureView(FRHITextureView* View) = 0;
	[[nodiscard]] virtual FRHIDescriptorSetLayout* CreateDescriptorSetLayout(const FRHIDescriptorSetLayoutDesc& Desc) = 0;
	virtual void DestroyDescriptorSetLayout(FRHIDescriptorSetLayout* Layout) = 0;
	[[nodiscard]] virtual FRHIPipelineLayout* CreatePipelineLayout(const FRHIPipelineLayoutDesc& Desc) = 0;
	virtual void DestroyPipelineLayout(FRHIPipelineLayout* Layout) = 0;
	[[nodiscard]] virtual FRHIDescriptorPool* CreateDescriptorPool(const FRHIDescriptorPoolDesc& Desc) = 0;
	virtual void DestroyDescriptorPool(FRHIDescriptorPool* Pool) = 0;
	[[nodiscard]] virtual FRHIDescriptorSet* AllocateDescriptorSet(FRHIDescriptorPool* Pool, FRHIDescriptorSetLayout* Layout) = 0;
	virtual void FreeDescriptorSet(FRHIDescriptorPool* Pool, FRHIDescriptorSet* Set) = 0;
	/**
	 * Write descriptor set contents (device-level -- maps to vkUpdateDescriptorSets,
	 * an immediate CPU op, and is NOT a recorded vkCmd). No command buffer needed;
	 * callable whenever the set + referenced resources are valid.
	 */
	virtual void UpdateDescriptorSets(const FRHIDescriptorWrite* Writes, std::uint32_t Count) = 0;
	[[nodiscard]] virtual FRHIRenderPass* CreateRenderPass(const FRHIRenderPassDesc& Desc) = 0;
	virtual void DestroyRenderPass(FRHIRenderPass* Pass) = 0;
	[[nodiscard]] virtual FRHIFramebuffer* CreateFramebuffer(const FRHIFramebufferDesc& Desc) = 0;
	virtual void DestroyFramebuffer(FRHIFramebuffer* Framebuffer) = 0;

	// Swapchain stays device-private; only the framebuffer size is exposed (the
	// scene target must match it, and its format must match GetSwapchainFormat).
	[[nodiscard]] virtual std::uint32_t GetFramebufferWidth() const = 0;
	[[nodiscard]] virtual std::uint32_t GetFramebufferHeight() const = 0;

	// GPU queries (occlusion / timestamp)
	[[nodiscard]] virtual FRHIQueryPool* CreateQueryPool(ERHIQueryType Type, std::uint32_t QueryCount) = 0;
	virtual void DestroyQueryPool(FRHIQueryPool* Pool) = 0;

	/**
	 * Copy query results to the destination buffer (GPU) or CPU memory.
	 * When bWait is true this blocks until results are available (a
	 		 * synchronized read - call off the RHI thread).
	 */
	virtual bool GetQueryPoolResults(
		FRHIQueryPool* Pool,
		std::uint32_t FirstQuery,
		std::uint32_t QueryCount,
		std::uint64_t* Results,
		std::size_t Stride,
		bool bWait = true) = 0;

	// Ray tracing
	[[nodiscard]] virtual FRHIRayTracingPipeline* CreateRayTracingPipeline(const FRHIRayTracingPipelineDesc& Desc) = 0;
	virtual void DestroyRayTracingPipeline(FRHIRayTracingPipeline* Pipeline) = 0;

	/**
	 * Create an acceleration structure (BLAS or TLAS). The structure is built
	 * later on a command list via FRHICommandList::BuildAccelerationStructure.
	 * Returns nullptr when the device lacks ray tracing support.
	 */
	[[nodiscard]] virtual FRHIAccelerationStructure* CreateAccelerationStructure(const FRHIRayTracingGeometryDesc& Desc) = 0;
	virtual void DestroyAccelerationStructure(FRHIAccelerationStructure* Accel) = 0;

	/** Query the required device sizes (accel + scratch) for a geometry desc. */
	virtual bool GetAccelerationStructureBuildSizes(
		const FRHIRayTracingGeometryDesc& Desc,
		std::uint64_t& OutAccelSize,
		std::uint64_t& OutScratchSize) = 0;

	/**
	 * Create a shader binding table from the pipeline's stage groups.
	 * Returns an opaque handle to the table's GPU-side layout (the caller
	 * supplies it to TraceRays via FRHIRayTracingSbt). The buffer is
	 * DeviceAddress-capable and Storage-flagged.
	 */
	[[nodiscard]] virtual FRHIBuffer* CreateShaderBindingTable(
		FRHIRayTracingPipeline* Pipeline,
		const FRHISbtGroup* Groups,
		std::uint32_t GroupCount,
		std::uint32_t* OutRayGenOffset = nullptr,
		std::uint32_t* OutRayGenStride = nullptr,
		std::uint32_t* OutHitOffset = nullptr,
		std::uint32_t* OutHitStride = nullptr,
		std::uint32_t* OutMissOffset = nullptr,
		std::uint32_t* OutMissStride = nullptr) = 0;

protected:
	IDynamicRHI() = default;
};

/** Creates the RHI backend selected by FRHIInitDesc::Backend. */
class MAHO_RHI_API FRHIFactory
{
public:
	FRHIFactory() = delete;

	[[nodiscard]] static IDynamicRHI* Create(ERHIBackend Backend);
};

} // namespace Maho
