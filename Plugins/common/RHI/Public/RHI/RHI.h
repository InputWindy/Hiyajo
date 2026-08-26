#pragma once

#include "RHIAPI.h"
#include <RHI/RHICommandList.h>
#include <RHI/RHIEnums.h>
#include <RHI/RHIResourceManager.h>
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
 * Device creates resources; prefer FRHIResourceManager for Acquire/Release.
 * Always exposes Graphics / Compute / Transfer logical queues.
 */
class MAHO_RHI_API IRHI
{
public:
	virtual ~IRHI() = default;

	IRHI(const IRHI&) = delete;
	IRHI& operator=(const IRHI&) = delete;

	virtual bool Initialize(const FRHIInitDesc& Desc) = 0;
	virtual void Shutdown() = 0;

	virtual void BeginFrame() = 0;
	virtual void Clear(float R, float G, float B, float A) = 0;
	virtual void EndFrame() = 0;

	virtual void Resize(int Width, int Height) = 0;

	[[nodiscard]] virtual bool IsInitialized() const = 0;

	[[nodiscard]] virtual FRHIResourceManager& GetResourceManager() = 0;
	[[nodiscard]] virtual IRHIMemoryAllocator* GetMemoryAllocator() = 0;

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

	[[nodiscard]] virtual FRHISemaphore* CreateGpuSemaphore() = 0;
	virtual void DestroyGpuSemaphore(FRHISemaphore* Semaphore) = 0;

	virtual void UpdateBuffer(FRHIBuffer* Buffer, std::uint64_t Offset, std::uint64_t Size, const void* Data) = 0;
	virtual void UpdateDescriptorSets(const FRHIDescriptorWrite* Writes, std::uint32_t Count) = 0;

	/** Internal factories used by FRHIResourceManager. Prefer Acquire* on the manager. */
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
	[[nodiscard]] virtual FRHIRenderPass* CreateRenderPass(const FRHIRenderPassDesc& Desc) = 0;
	virtual void DestroyRenderPass(FRHIRenderPass* Pass) = 0;
	[[nodiscard]] virtual FRHIFramebuffer* CreateFramebuffer(const FRHIFramebufferDesc& Desc) = 0;
	virtual void DestroyFramebuffer(FRHIFramebuffer* Framebuffer) = 0;

	// GPU queries (occlusion / timestamp)
	[[nodiscard]] virtual FRHIQueryPool* CreateQueryPool(ERHIQueryType Type, std::uint32_t QueryCount) = 0;
	virtual void DestroyQueryPool(FRHIQueryPool* Pool) = 0;

	/**
	 * Copy query results to the destination buffer (GPU) or CPU memory.
	 * When bWait is true this blocks until results are available (a
	 * synchronized read — call off the RHI thread).
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
	IRHI() = default;
};

/** Deleter that frees the object inside Maho.dll (safe across EXE/DLL heaps). */
struct MAHO_RHI_API FRHIDeleter
{
	void operator()(IRHI* RHI) const;
};

using FRHIPtr = std::unique_ptr<IRHI, FRHIDeleter>;

/** Creates the RHI backend selected by FRHIInitDesc::Backend. */
class MAHO_RHI_API FRHIFactory
{
public:
	FRHIFactory() = delete;

	[[nodiscard]] static FRHIPtr Create(ERHIBackend Backend);
};

} // namespace Maho
