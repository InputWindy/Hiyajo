#pragma once

#include "RHIAPI.h"
#include <Core/Singleton.h>
#include <Core/ThreadPool.h>
#include <Core/ThreadedServer.h>
#include <Maho.h>
#include <RHI/RHICommandList.h>
#include <RHI/RHIEnums.h>
#include <RHI/RHIResources.h>

#include <condition_variable>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>

// Opaque Vulkan handle aliases -- the ONLY Vulkan types that escape the RHI,
// and only via the narrow ImGui official-backend bridge below (imgui_impl_vulkan
// needs raw handles). Declared at GLOBAL scope so the RHI's own TUs (which use
// the real vulkan.h types) are not shadowed by a Maho:: namespace alias; the
// definitions (from vulkan.h) are identical typedefs, so no conflict.
struct VkInstance_T;         using VkInstance = VkInstance_T*;
struct VkPhysicalDevice_T;   using VkPhysicalDevice = VkPhysicalDevice_T*;
struct VkDevice_T;           using VkDevice = VkDevice_T*;
struct VkQueue_T;            using VkQueue = VkQueue_T*;
struct VkImageView_T;        using VkImageView = VkImageView_T*;

namespace Maho
{

/** Public capability surface of the RHI layer (driven by the parent via Query). */
struct IRHI
{
	virtual ~IRHI() = default;

	/**
	 * Create a command list for the given type. Command-list LIFECYCLE belongs
	 		 * to the caller (e.g. the RDG, which owns frame isolation) - the RHI only
	 * provides the raw object. Record via EnqueueTask, destroy with
	 * DestroyCommandList.
	 */
	[[nodiscard]] virtual FRHICommandList* CreateCommandList(ERHICommandListType Type) = 0;
	virtual void DestroyCommandList(FRHICommandList* CmdList) = 0;

	/**
	 * Submit a recorded command list on the RHI worker, routing to the queue
	 * that matches the command-list type (graphics/compute/transfer, honoring
	 * native-queue fallback). When to submit is the caller's (RDG) scheduling
	 		 * decision - this only performs the queue submit itself.
	 */
	virtual void Submit(
		FRHICommandList* CmdList,
		ERHICommandListType Type = ERHICommandListType::Graphics,
		FRHISemaphore* const* WaitSemaphores = nullptr,
		std::uint32_t WaitCount = 0,
		FRHISemaphore* const* SignalSemaphores = nullptr,
		std::uint32_t SignalCount = 0,
		FRHIFence* SignalFence = nullptr) = 0;

	/**
	 * Run a recording task on a worker thread from the pool (parallel command
	 		 * recording). The callback receives ITS OWN command list - never share a
	 * command list across tasks (Vulkan forbids concurrent recording into the
	 * same buffer). After tasks complete, the caller (RDG) submits the recorded
	 * command lists serially via Submit.
	 */
	virtual void EnqueueTask(
		FRHICommandList* CmdList,
		std::function<void(FRHICommandList*)> Task) = 0;

	/**
	 * Barrier: wait until every EnqueueTask submitted so far has finished
	 		 * recording. Call BEFORE Submit to guarantee the "record all - submit all"
	 * ordering when using parallel command recording.
	 */
	virtual void Flush() = 0;

	// -- frame primitives (call inside EnqueueTask to run on the server thread) --
	virtual void BeginFrame() = 0;
	virtual void EndFrame() = 0;
	virtual void Resize(int Width, int Height) = 0;

	/** Block until all submitted GPU work has completed (device idle). Call
	 *  BEFORE destroying resources that a submitted command buffer may still
	 *  reference (engine shutdown / swapchain teardown). */
	virtual void WaitIdle() = 0;

	/**
	 * Borrow the frame command buffer as a recording surface. The buffer is
	 * already begun by BeginFrame and will be ended/submitted by EndFrame -
	 * features only record render passes / draws into it. Non-owning; never call
	 * Begin/End on the returned list.
	 */
	[[nodiscard]] virtual FRHICommandList* GetFrameCommandList() = 0;

	/**
	 * Copy an off-screen texture to the current swapchain backbuffer (blit) on
	 * the frame command buffer. Call after all scene recording, before EndFrame.
	 * The swapchain render pass / framebuffer stay RHI-private.
	 */
	virtual void PresentTexture(FRHITexture* Src) = 0;

	/** Current swapchain image format (off-screen scene targets must match it for blit). */
	[[nodiscard]] virtual ERHIFormat GetSwapchainFormat() const = 0;

	[[nodiscard]] virtual bool IsInitialized() const = 0;

	[[nodiscard]] virtual FRHIFence* CreateFence(bool bSignaled) = 0;
	virtual void DestroyFence(FRHIFence* Fence) = 0;
	virtual void WaitForFence(FRHIFence* Fence, std::uint64_t TimeoutNs = (std::numeric_limits<std::uint64_t>::max)()) = 0;
	/** Non-blocking fence query (vkGetFenceStatus). */
	[[nodiscard]] virtual bool IsFenceSignaled(FRHIFence* Fence) = 0;

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
	[[nodiscard]] virtual FRHIRenderPass* CreateRenderPass(const FRHIRenderPassDesc& Desc) = 0;
	virtual void DestroyRenderPass(FRHIRenderPass* Pass) = 0;
	[[nodiscard]] virtual FRHIFramebuffer* CreateFramebuffer(const FRHIFramebufferDesc& Desc) = 0;
	virtual void DestroyFramebuffer(FRHIFramebuffer* Framebuffer) = 0;

	// Swapchain stays RHI-private: features render off-screen and hand FRender a
	// color texture, which PresentTexture blits to the backbuffer. Only the
	// format and framebuffer size are exposed (scene targets must match both).
	[[nodiscard]] virtual std::uint32_t GetFramebufferWidth() const = 0;
	[[nodiscard]] virtual std::uint32_t GetFramebufferHeight() const = 0;

	// -- ImGui official-backend bridge (narrow exception to the backend-agnostic
	//    surface; ONLY for imgui_impl_vulkan, which needs raw Vulkan handles).
	//    Do not add more raw-Vulkan escapes -- other layers use the abstractions.
	[[nodiscard]] virtual VkInstance GetRawInstance() const = 0;
	[[nodiscard]] virtual VkPhysicalDevice GetRawPhysicalDevice() const = 0;
	[[nodiscard]] virtual VkDevice GetRawDevice() const = 0;
	[[nodiscard]] virtual VkQueue GetRawGraphicsQueue() const = 0;
	[[nodiscard]] virtual std::uint32_t GetRawGraphicsQueueFamilyIndex() const = 0;
	[[nodiscard]] virtual std::uint32_t GetRawSwapchainImageCount() const = 0;
	/** Raw image view of an RHI texture view (ImGui renders into SceneColor). */
	[[nodiscard]] virtual VkImageView GetRawTextureView(FRHITextureView* View) const = 0;

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
};


class IDynamicRHI;

/**
 * RHI - backend-agnostic GPU device surface, a RENDER SERVER (FThreadedServer),
 * NOT a scheduled layer. Hosts the IDynamicRHI device and exposes the IRHI
 * capability surface. The render owner (FRender) holds it and interacts only
 * through the IRHI command surface (EnqueueTask / Submit / frame primitives) -
 * the render thread is the server thread. Command recording is parallel via
 * EnqueueTask (thread pool); queue submits and frame primitives are direct
 * calls - the caller (RDG) keeps them serial. Backend-agnostic - higher layers
 * (RDG / render plugin) never touch a concrete backend type. The device itself
 * (IDynamicRHI) stays private to this DLL.
 */
class FRHI final
	: public FThreadedServer
	, public IRHI
{
public:
	FRHI();
	virtual ~FRHI() override;

	/** Bring the render server up (start the thread) + initialize the device. */
	bool Initialize(void* NativeWindowHandle, int Width, int Height,
		ERHIBackend Backend = ERHIBackend::Vulkan);

	/** Tear the device + server down (idempotent). */
	void ShutdownRHI();

	/**
	 * Record commands into a caller-owned command list on a thread-pool worker
		 * (parallel recording). The caller (RDG) owns the command list's lifecycle
		 * (frame isolation) and decides WHEN to submit - call Submit explicitly.
	 */
	void EnqueueTask(
		FRHICommandList* CmdList,
		std::function<void(FRHICommandList*)> Task) override;
	void Flush() override;

	[[nodiscard]] FRHICommandList* CreateCommandList(ERHICommandListType Type) override;
	void DestroyCommandList(FRHICommandList* CmdList) override;
	void Submit(
		FRHICommandList* CmdList,
		ERHICommandListType Type = ERHICommandListType::Graphics,
		FRHISemaphore* const* WaitSemaphores = nullptr,
		std::uint32_t WaitCount = 0,
		FRHISemaphore* const* SignalSemaphores = nullptr,
		std::uint32_t SignalCount = 0,
		FRHIFence* SignalFence = nullptr) override;

	// -- IRHI frame primitives (forward to the private IDynamicRHI) --
	void BeginFrame() override;
	void EndFrame() override;
	void Resize(int Width, int Height) override;
	void WaitIdle() override;
	[[nodiscard]] FRHICommandList* GetFrameCommandList() override;
	void PresentTexture(FRHITexture* Src) override;
	[[nodiscard]] ERHIFormat GetSwapchainFormat() const override;

	// -- IRHI device methods (forward to the private IDynamicRHI) --
	[[nodiscard]] bool IsInitialized() const override;

	[[nodiscard]] FRHIFence* CreateFence(bool bSignaled) override;
	void DestroyFence(FRHIFence* Fence) override;
	void WaitForFence(FRHIFence* Fence, std::uint64_t TimeoutNs) override;
	[[nodiscard]] bool IsFenceSignaled(FRHIFence* Fence) override;

	[[nodiscard]] FRHISemaphore* CreateGpuSemaphore() override;
	void DestroyGpuSemaphore(FRHISemaphore* Semaphore) override;

	[[nodiscard]] FRHIBuffer* CreateBuffer(const FRHIBufferDesc& Desc) override;
	void DestroyBuffer(FRHIBuffer* Buffer) override;
	[[nodiscard]] FRHITexture* CreateTexture(const FRHITextureDesc& Desc) override;
	void DestroyTexture(FRHITexture* Texture) override;
	[[nodiscard]] FRHISampler* CreateSampler(const FRHISamplerDesc& Desc) override;
	void DestroySampler(FRHISampler* Sampler) override;
	[[nodiscard]] FRHIShaderModule* CreateShaderModule(const FRHIShaderModuleDesc& Desc) override;
	void DestroyShaderModule(FRHIShaderModule* Module) override;
	[[nodiscard]] FRHIGraphicsPipeline* CreateGraphicsPipeline(const FRHIGraphicsPipelineDesc& Desc) override;
	void DestroyGraphicsPipeline(FRHIGraphicsPipeline* Pipeline) override;
	[[nodiscard]] FRHIComputePipeline* CreateComputePipeline(const FRHIComputePipelineDesc& Desc) override;
	void DestroyComputePipeline(FRHIComputePipeline* Pipeline) override;

	[[nodiscard]] FRHIStructuredBuffer* CreateStructuredBuffer(const FRHIStructuredBufferDesc& Desc) override;
	void DestroyStructuredBuffer(FRHIStructuredBuffer* Buffer) override;
	[[nodiscard]] FRHIBufferView* CreateBufferView(const FRHIBufferViewDesc& Desc) override;
	void DestroyBufferView(FRHIBufferView* View) override;

	[[nodiscard]] FRHITextureView* CreateTextureView(const FRHITextureViewDesc& Desc) override;
	void DestroyTextureView(FRHITextureView* View) override;
	[[nodiscard]] FRHIDescriptorSetLayout* CreateDescriptorSetLayout(const FRHIDescriptorSetLayoutDesc& Desc) override;
	void DestroyDescriptorSetLayout(FRHIDescriptorSetLayout* Layout) override;
	[[nodiscard]] FRHIPipelineLayout* CreatePipelineLayout(const FRHIPipelineLayoutDesc& Desc) override;
	void DestroyPipelineLayout(FRHIPipelineLayout* Layout) override;
	[[nodiscard]] FRHIDescriptorPool* CreateDescriptorPool(const FRHIDescriptorPoolDesc& Desc) override;
	void DestroyDescriptorPool(FRHIDescriptorPool* Pool) override;
	[[nodiscard]] FRHIDescriptorSet* AllocateDescriptorSet(FRHIDescriptorPool* Pool, FRHIDescriptorSetLayout* Layout) override;
	void FreeDescriptorSet(FRHIDescriptorPool* Pool, FRHIDescriptorSet* Set) override;
	[[nodiscard]] FRHIRenderPass* CreateRenderPass(const FRHIRenderPassDesc& Desc) override;
	void DestroyRenderPass(FRHIRenderPass* Pass) override;
	[[nodiscard]] FRHIFramebuffer* CreateFramebuffer(const FRHIFramebufferDesc& Desc) override;
	void DestroyFramebuffer(FRHIFramebuffer* Framebuffer) override;

	// -- IRHI swapchain accessors (forward to the private IDynamicRHI) --
	[[nodiscard]] std::uint32_t GetFramebufferWidth() const override;
	[[nodiscard]] std::uint32_t GetFramebufferHeight() const override;

	// -- IRHI ImGui bridge (forward to the private IDynamicRHI) --
	[[nodiscard]] VkInstance GetRawInstance() const override;
	[[nodiscard]] VkPhysicalDevice GetRawPhysicalDevice() const override;
	[[nodiscard]] VkDevice GetRawDevice() const override;
	[[nodiscard]] VkQueue GetRawGraphicsQueue() const override;
	[[nodiscard]] std::uint32_t GetRawGraphicsQueueFamilyIndex() const override;
	[[nodiscard]] std::uint32_t GetRawSwapchainImageCount() const override;
	[[nodiscard]] VkImageView GetRawTextureView(FRHITextureView* View) const override;

	[[nodiscard]] FRHIQueryPool* CreateQueryPool(ERHIQueryType Type, std::uint32_t QueryCount) override;
	void DestroyQueryPool(FRHIQueryPool* Pool) override;
	bool GetQueryPoolResults(
		FRHIQueryPool* Pool,
		std::uint32_t FirstQuery,
		std::uint32_t QueryCount,
		std::uint64_t* Results,
		std::size_t Stride,
		bool bWait) override;

	[[nodiscard]] FRHIRayTracingPipeline* CreateRayTracingPipeline(const FRHIRayTracingPipelineDesc& Desc) override;
	void DestroyRayTracingPipeline(FRHIRayTracingPipeline* Pipeline) override;
	[[nodiscard]] FRHIAccelerationStructure* CreateAccelerationStructure(const FRHIRayTracingGeometryDesc& Desc) override;
	void DestroyAccelerationStructure(FRHIAccelerationStructure* Accel) override;
	bool GetAccelerationStructureBuildSizes(
		const FRHIRayTracingGeometryDesc& Desc,
		std::uint64_t& OutAccelSize,
		std::uint64_t& OutScratchSize) override;
	[[nodiscard]] FRHIBuffer* CreateShaderBindingTable(
		FRHIRayTracingPipeline* Pipeline,
		const FRHISbtGroup* Groups,
		std::uint32_t GroupCount,
		std::uint32_t* OutRayGenOffset,
		std::uint32_t* OutRayGenStride,
		std::uint32_t* OutHitOffset,
		std::uint32_t* OutHitStride,
		std::uint32_t* OutMissOffset,
		std::uint32_t* OutMissStride) override;

private:
	std::unique_ptr<IDynamicRHI> RHI;
	FThreadPool RecordingPool{1};   // serial recording worker -- keeps dependent submits ordered
};

} // namespace Maho
