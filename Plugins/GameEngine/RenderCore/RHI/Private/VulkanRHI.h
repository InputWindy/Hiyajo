#pragma once

#include "RHI.h"

#include "VulkanCommandList.h"
#include "VulkanMemory.h"
#include "VulkanResources.h"

#include <Core/FrameGraph.h>   // MAHO_FRAMES_IN_FLIGHT: the frame-state slot count (see kFrameSlots)

#include <array>
#include <memory>
#include <vector>

#include <vulkan/vulkan.h>

namespace Maho
{

/** Minimal Vulkan implementation of IDynamicRHI. Not part of the public Maho API surface. */
class FVulkanRHI final : public IDynamicRHI
{
public:
	FVulkanRHI();
	~FVulkanRHI() override;

	virtual bool Initialize(const FRHIInitDesc& Desc) override;
	virtual void Shutdown() override;

	virtual void BeginFrame() override;
	virtual void EndFrame() override;
	virtual void Resize(int Width, int Height) override;
	virtual void WaitIdle() override;

	virtual void PresentTexture(FRHITexture* Src) override;

	/** Arm / cancel the capture recorded inside the next PresentTexture and flushed one boundary
	 *  later. See IRHI::RequestBackbufferCapture. */
	virtual void RequestBackbufferCapture(FBackbufferCaptureSink Sink) override;

	[[nodiscard]] virtual ERHIFormat GetSwapchainFormat() const override;
	[[nodiscard]] virtual bool IsFormatSupported(ERHIFormat Format, ERHITextureUsage Usage) const override;
	virtual bool ReadbackTexture(
		FRHITexture* Src,
		const FRHITextureCopyRegion& Region,
		void* Out,
		std::uint64_t OutSize,
		ERHIResourceState PreserveState) override;

	[[nodiscard]] virtual bool IsInitialized() const override;

	[[nodiscard]] virtual IDynamicRHIMemoryAllocator* GetMemoryAllocator() override;

	[[nodiscard]] virtual FRHIQueue& GetGraphicsQueue() override;
	[[nodiscard]] virtual FRHIQueue& GetComputeQueue() override;
	[[nodiscard]] virtual FRHIQueue& GetTransferQueue() override;

	[[nodiscard]] virtual FRHICommandList* CreateCommandList(ERHICommandListType Type) override;
	virtual void DestroyCommandList(FRHICommandList* CmdList) override;

	[[nodiscard]] virtual FRHIFence* CreateFence(bool bSignaled) override;
	virtual void DestroyFence(FRHIFence* Fence) override;
	virtual void WaitForFence(FRHIFence* Fence, std::uint64_t TimeoutNs) override;
	[[nodiscard]] virtual bool IsFenceSignaled(FRHIFence* Fence) override;
	virtual void ResetFence(FRHIFence* Fence) override;

	[[nodiscard]] virtual FRHISemaphore* CreateGpuSemaphore() override;
	virtual void DestroyGpuSemaphore(FRHISemaphore* Semaphore) override;


	[[nodiscard]] virtual FRHIBuffer* CreateBuffer(const FRHIBufferDesc& Desc) override;
	virtual void DestroyBuffer(FRHIBuffer* Buffer) override;
	[[nodiscard]] virtual FRHITexture* CreateTexture(const FRHITextureDesc& Desc) override;
	virtual void DestroyTexture(FRHITexture* Texture) override;
	[[nodiscard]] virtual FRHISampler* CreateSampler(const FRHISamplerDesc& Desc) override;
	virtual void DestroySampler(FRHISampler* Sampler) override;
	[[nodiscard]] virtual FRHIShaderModule* CreateShaderModule(const FRHIShaderModuleDesc& Desc) override;
	virtual void DestroyShaderModule(FRHIShaderModule* Module) override;
	[[nodiscard]] virtual FRHIGraphicsPipeline* CreateGraphicsPipeline(const FRHIGraphicsPipelineDesc& Desc) override;
	virtual void DestroyGraphicsPipeline(FRHIGraphicsPipeline* Pipeline) override;
	[[nodiscard]] virtual FRHIComputePipeline* CreateComputePipeline(const FRHIComputePipelineDesc& Desc) override;
	virtual void DestroyComputePipeline(FRHIComputePipeline* Pipeline) override;

	[[nodiscard]] virtual FRHIStructuredBuffer* CreateStructuredBuffer(const FRHIStructuredBufferDesc& Desc) override;
	virtual void DestroyStructuredBuffer(FRHIStructuredBuffer* Buffer) override;
	[[nodiscard]] virtual FRHIBufferView* CreateBufferView(const FRHIBufferViewDesc& Desc) override;
	virtual void DestroyBufferView(FRHIBufferView* View) override;

	[[nodiscard]] virtual FRHITextureView* CreateTextureView(const FRHITextureViewDesc& Desc) override;
	virtual void DestroyTextureView(FRHITextureView* View) override;
	[[nodiscard]] virtual FRHIDescriptorSetLayout* CreateDescriptorSetLayout(const FRHIDescriptorSetLayoutDesc& Desc) override;
	virtual void DestroyDescriptorSetLayout(FRHIDescriptorSetLayout* Layout) override;
	[[nodiscard]] virtual FRHIPipelineLayout* CreatePipelineLayout(const FRHIPipelineLayoutDesc& Desc) override;
	virtual void DestroyPipelineLayout(FRHIPipelineLayout* Layout) override;
	[[nodiscard]] virtual FRHIDescriptorPool* CreateDescriptorPool(const FRHIDescriptorPoolDesc& Desc) override;
	virtual void DestroyDescriptorPool(FRHIDescriptorPool* Pool) override;
	[[nodiscard]] virtual FRHIDescriptorSet* AllocateDescriptorSet(FRHIDescriptorPool* Pool, FRHIDescriptorSetLayout* Layout) override;
	virtual void FreeDescriptorSet(FRHIDescriptorPool* Pool, FRHIDescriptorSet* Set) override;
	virtual void UpdateDescriptorSets(const FRHIDescriptorWrite* Writes, std::uint32_t Count) override;
	[[nodiscard]] virtual FRHIRenderPass* CreateRenderPass(const FRHIRenderPassDesc& Desc) override;
	virtual void DestroyRenderPass(FRHIRenderPass* Pass) override;
	[[nodiscard]] virtual FRHIFramebuffer* CreateFramebuffer(const FRHIFramebufferDesc& Desc) override;
	virtual void DestroyFramebuffer(FRHIFramebuffer* Framebuffer) override;

	// Swapchain stays device-private; only framebuffer size is exposed.
	[[nodiscard]] virtual std::uint32_t GetFramebufferWidth() const override;
	[[nodiscard]] virtual std::uint32_t GetFramebufferHeight() const override;

	[[nodiscard]] virtual FRHIQueryPool* CreateQueryPool(ERHIQueryType Type, std::uint32_t QueryCount) override;
	virtual void DestroyQueryPool(FRHIQueryPool* Pool) override;
	virtual bool GetQueryPoolResults(
		FRHIQueryPool* Pool,
		std::uint32_t FirstQuery,
		std::uint32_t QueryCount,
		std::uint64_t* Results,
		std::size_t Stride,
		bool bWait) override;

	[[nodiscard]] virtual FRHIRayTracingPipeline* CreateRayTracingPipeline(const FRHIRayTracingPipelineDesc& Desc) override;
	virtual void DestroyRayTracingPipeline(FRHIRayTracingPipeline* Pipeline) override;
	[[nodiscard]] virtual FRHIAccelerationStructure* CreateAccelerationStructure(const FRHIRayTracingGeometryDesc& Desc) override;
	virtual void DestroyAccelerationStructure(FRHIAccelerationStructure* Accel) override;
	virtual bool GetAccelerationStructureBuildSizes(
		const FRHIRayTracingGeometryDesc& Desc,
		std::uint64_t& OutAccelSize,
		std::uint64_t& OutScratchSize) override;
	[[nodiscard]] virtual FRHIBuffer* CreateShaderBindingTable(
		FRHIRayTracingPipeline* Pipeline,
		const FRHISbtGroup* Groups,
		std::uint32_t GroupCount,
		std::uint32_t* OutRayGenOffset,
		std::uint32_t* OutRayGenStride,
		std::uint32_t* OutHitOffset,
		std::uint32_t* OutHitStride,
		std::uint32_t* OutMissOffset,
		std::uint32_t* OutMissStride) override;

	[[nodiscard]] VkInstance GetVkInstance() const
	{
		return Instance;
	}
	[[nodiscard]] VkPhysicalDevice GetVkPhysicalDevice() const
	{
		return PhysicalDevice;
	}
	[[nodiscard]] VkDevice GetVkDevice() const
	{
		return Device;
	}
	[[nodiscard]] VkQueue GetVkGraphicsQueue() const
	{
		return GraphicsVkQueue;
	}
	[[nodiscard]] std::uint32_t GetGraphicsQueueFamilyIndex() const
	{
		return GraphicsQueueFamilyIndex;
	}
	[[nodiscard]] VkRenderPass GetVkRenderPass() const
	{
		return RenderPass;
	}
	[[nodiscard]] VkCommandBuffer GetVkCommandBuffer() const
	{
		return CommandBuffer;
	}
	[[nodiscard]] std::uint32_t GetSwapchainImageCount() const
	{
		return static_cast<std::uint32_t>(SwapchainImages.size());
	}
	[[nodiscard]] std::uint32_t GetMinImageCount() const;

public:
	/** Frame-state slot count -- the number of frames this device lets overlap. It IS
	 *  `MAHO_FRAMES_IN_FLIGHT` (the graph's phase-ring depth) and that is the whole point: before this
	 *  existed the device held ONE fence and one acquired image, so `BeginFrame` waited for the
	 *  IMMEDIATELY previous submit and the pipeline depth was 1 while the graph declared 3. A frame's
	 *  per-frame resources (a camera ring, a transient pool) must be sized by the depth that is ACTUALLY
	 *  enforced -- so the enforced depth is now the declared one, and EVERY slot's fence is waited
	 *  before that slot is used again. */
	static constexpr std::uint32_t kFrameSlots = MAHO_FRAMES_IN_FLIGHT;

	// -- frame-state aliases for the CURRENT slot ---------------------------------------------
	//
	// Assigned by BeginFrame from the slot arrays below. The recording path (the present blit, the
	// capture copy, the transitions) reads THESE, so a frame always touches its own slot's command
	// buffer and its own slot's semaphores -- and the frame it belongs to is decided once, at the
	// frame head, instead of being re-derived at every use site.
	VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
	FRHICommandList* FrameCommandListRHI = nullptr;
	VkFence InFlightFence = VK_NULL_HANDLE;
	VkSemaphore ImageAvailableSemaphore = VK_NULL_HANDLE;

	/** Per-slot frame state. One entry per kFrameSlots: the frame's command buffer (+ its recording
	 *  wrapper), the fence its submit signals, the acquire semaphore its submit waits, and the image
	 *  index the acquire returned. K copies is what makes "frame N+1 may overlap frame N" legal. */
	std::array<VkCommandBuffer, kFrameSlots> FrameCommandBuffers{};
	std::array<FRHICommandList*, kFrameSlots> FrameCommandLists{};
	std::array<VkFence, kFrameSlots> InFlightFences{};
	std::array<VkSemaphore, kFrameSlots> FrameAcquireSemaphores{};
	std::array<std::uint32_t, kFrameSlots> FrameImageIndices{};

	/** The slot the frame in progress uses; advanced once per BeginFrame/EndFrame pair. */
	std::uint32_t CurrentSlot = 0;

	// -- frame-depth probe (bounded diagnostic; see GCVarRHI_FrameDepth in the .cpp) -----------
	//
	// Server-thread-only counters (no atomics: every one of them is touched on the RHI server thread,
	// which is the only emitter). Each head waits ONE slot's fence, which proves the frame `kFrameSlots`
	// back complete -- that, and only that, is what `RetiredFrames` counts. So `Opened - Retired` is the
	// number of frames whose per-frame data may still be in the GPU's hands, i.e. the lower bound every
	// frame-domain ring (the camera ring, the transient pool) must meet; it settles at `kFrameSlots`
	// once the pipeline is full. Measuring it is what keeps the declaration honest.
	std::uint64_t OpenedFrames = 0;      // frame heads that opened a slot (== the frame being recorded + 1)
	std::uint64_t SubmittedFrames = 0;   // frame tails that submitted it
	std::uint64_t RetiredFrames = 0;     // frames PROVEN complete: opened - kFrameSlots, clamped at 0
	std::uint64_t PeakAhead = 0;         // peak of Opened - Retired (== kFrameSlots in steady state)
	std::uint64_t PeakInFlight = 0;      // peak of Submitted - Retired (what the queue actually holds)

private:
	bool CreateInstance();
	void CreateDebugMessenger();
	bool CreateSurface();
	bool PickPhysicalDevice();
	bool CreateLogicalDevice();
	bool CreateSwapchain();
	void DestroySwapchainResources();
	bool CreateImageViews();
	bool CreateRenderPass();
	bool CreateFramebuffers();
	bool CreateCommandPoolAndBuffer();
	bool CreateLogicalQueuesAndPools();
	bool CreateMemoryAllocator();
	bool CreateSyncObjects();
	bool CreateRenderFinishedSemaphores();
	bool RecreateSwapchain();

	[[nodiscard]] bool IsDeviceSuitable(VkPhysicalDevice InPhysicalDevice);
	[[nodiscard]] bool FindQueueFamilies(VkPhysicalDevice InPhysicalDevice);
	[[nodiscard]] bool CheckDeviceExtensionSupport(VkPhysicalDevice InPhysicalDevice) const;

	[[nodiscard]] VkCommandPool GetPoolForType(ERHICommandListType Type) const;
	[[nodiscard]] static VkBufferUsageFlags ToVkBufferUsage(ERHIBufferUsage Usage);
	[[nodiscard]] static VkImageUsageFlags ToVkImageUsage(ERHITextureUsage Usage);
	[[nodiscard]] static VkFormat ToVkFormat(ERHIFormat Format);
	/** The ONE VkFormat -> ERHIFormat mapper (inverse of ToVkFormat). Unknown / unmapped
	 *  Vulkan formats answer ERHIFormat::Unknown -- never a lossy guess at a similar format,
	 *  because that is how a swapchain's sRGB-ness used to disappear on the way out. */
	[[nodiscard]] static ERHIFormat ToRHIFormat(VkFormat Format);
	[[nodiscard]] static VkDescriptorType ToVkDescriptorType(ERHIDescriptorType Type);
	[[nodiscard]] static VkFilter ToVkFilter(ERHIFilter Filter);
	[[nodiscard]] static VkSamplerAddressMode ToVkAddressMode(ERHIAddressMode Mode);

	void* NativeWindowHandle = nullptr;
	int FramebufferWidth = 0;
	int FramebufferHeight = 0;

	bool bInitialized = false;

	VkInstance Instance = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT DebugMessenger = VK_NULL_HANDLE;
	VkSurfaceKHR Surface = VK_NULL_HANDLE;
	VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
	VkDevice Device = VK_NULL_HANDLE;

	VkQueue GraphicsVkQueue = VK_NULL_HANDLE;
	VkQueue PresentQueue = VK_NULL_HANDLE;
	VkQueue ComputeVkQueue = VK_NULL_HANDLE;
	VkQueue TransferVkQueue = VK_NULL_HANDLE;

	FVulkanQueue GraphicsQueue;
	FVulkanQueue ComputeQueue;
	FVulkanQueue TransferQueue;

	VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
	VkFormat SwapchainImageFormat = VK_FORMAT_UNDEFINED;
	VkExtent2D SwapchainExtent{};
	std::vector<VkImage> SwapchainImages;
	std::vector<VkImageView> SwapchainImageViews;
	std::vector<VkFramebuffer> SwapchainFramebuffers;

	/**
	 * Per-swapchain-image TRACKED state (one entry per SwapchainImages). Swapchain images are raw
	 * VkImages, not FRHITexture objects, so they cannot carry the per-texture tracked state -- the
	 * device keeps it here instead, and every transition on them (present blit, capture readback,
	 * present-src) goes through TransitionTrackedImage.
	 *
	 * Reset to UNDEFINED on REACQUIRE, not just on recreation: an image handed back by the
	 * presentation engine has no contents this device may assume, so tracking restarts from scratch
	 * each frame (the initial state is also the only correct barrier SOURCE after an acquire).
	 */
	std::vector<FVulkanImageState> SwapchainImageStates;

	// Non-owning RHI views of the swapchain framebuffers + render pass (the
	// swapchain owns the Vk handles; these are only for BeginRenderPass).
	std::vector<FRHIFramebuffer*> SwapchainFramebufferRHI;
	FRHIRenderPass* SwapchainRenderPassRHI = nullptr;

	VkRenderPass RenderPass = VK_NULL_HANDLE;

	/** Frame path command pool (graphics family). Owns the per-slot frame command buffers. */
	VkCommandPool CommandPool = VK_NULL_HANDLE;

	VkCommandPool GraphicsCmdPool = VK_NULL_HANDLE;
	VkCommandPool ComputeCmdPool = VK_NULL_HANDLE;
	VkCommandPool TransferCmdPool = VK_NULL_HANDLE;

	/** One per swapchain image (indexed by the slot's acquired image): a present on image i waits
	 *  semaphore[i], and the swapchain's own image count is what bounds their reuse. */
	std::vector<VkSemaphore> RenderFinishedSemaphores;

	/**
	 * Backbuffer capture (see RequestBackbufferCapture). The sink is armed by the frame owner; the
	 * copy is recorded inside PresentTexture (where the backbuffer's layout is known to be
	 * TRANSFER_DST after the blit) and the sink runs at the NEXT BeginFrame, i.e. after the fence
	 * that covers it -- no stall, and the pixels are stable.
	 */
	FBackbufferCaptureSink PendingCaptureSink;
	bool bCaptureRecorded = false;
	/** The slot that RECORDED the pending capture. Only that slot's fence covers the copy, so the sink
	 *  runs at the first BeginFrame of that very slot -- kFrameSlots boundaries later, not the next
	 *  one (the next frame's fence covers a different submit). */
	std::uint32_t CaptureSlot = 0;
	VkBuffer CaptureStagingBuffer = VK_NULL_HANDLE;
	VmaAllocation CaptureStagingAllocation = nullptr;
	std::uint64_t CaptureStagingBytes = 0;

	/** Make sure the capture staging buffer can hold `NeededBytes` (recreated when the frame's
	 *  extent changed). Returns false when the allocation failed. */
	bool bEnsureCaptureStaging(std::uint64_t NeededBytes);

	/**
	 * Record one barrier on the FRAME command buffer through the tracked-state model: the source
	 * half comes from *State, the destination half from Target, and *State advances to Target. A
	 * no-op (returns false) when the image is already in Target's layout.
	 *
	 * It exists because the swapchain images are NOT FRHITexture objects -- they cannot carry the
	 * RHI's per-texture tracked state -- so the device keeps their states in SwapchainImageStates and
	 * uses this same helper. The presented SOURCE texture goes through the identical logic via
	 * FVulkanTexture's tracked state, which is what removes the last "assume the source is in
	 * COLOR_ATTACHMENT_OPTIMAL because a renderer wrote it" from the present path.
	 */
	bool TransitionTrackedImage(
		VkImage Image,
		FVulkanImageState& State,
		const FVulkanImageState& Target,
		VkImageAspectFlags AspectMask,
		std::uint32_t MipLevels,
		std::uint32_t Layers);

	std::uint32_t GraphicsQueueFamilyIndex = 0;
	std::uint32_t PresentQueueFamilyIndex = 0;
	std::uint32_t ComputeQueueFamilyIndex = 0;
	std::uint32_t TransferQueueFamilyIndex = 0;
	std::uint32_t GraphicsQueueIndex = 0;
	std::uint32_t ComputeQueueIndex = 0;
	std::uint32_t TransferQueueIndex = 0;
	bool bComputeNativeFallback = false;
	bool bTransferNativeFallback = false;

	std::uint32_t CurrentImageIndex = 0;

	float ClearColorR = 0.0f;
	float ClearColorG = 0.0f;
	float ClearColorB = 0.0f;
	float ClearColorA = 1.0f;

	bool bFramebufferResized = false;

	/** The r.VSync value the LIVE swapchain was created with. Compared against the cvar at each
	 *  frame boundary, because the present mode is fixed into the swapchain at creation -- a cvar
	 *  change alone is inert until something rebuilds it. -1 until a swapchain exists. */
	int ActiveVSync = -1;

	// Ray tracing: device-level functions (KHR extensions).
	bool bRayTracingSupported = false;
	PFN_vkCreateAccelerationStructureKHR CreateAccelerationStructureKHR = nullptr;
	PFN_vkDestroyAccelerationStructureKHR DestroyAccelerationStructureKHR = nullptr;
	PFN_vkGetAccelerationStructureBuildSizesKHR GetAccelerationStructureBuildSizesKHR = nullptr;
	PFN_vkCreateRayTracingPipelinesKHR CreateRayTracingPipelinesKHR = nullptr;
	PFN_vkGetRayTracingShaderGroupHandlesKHR GetRayTracingShaderGroupHandlesKHR = nullptr;
	PFN_vkCmdBuildAccelerationStructuresKHR CmdBuildAccelerationStructuresKHR = nullptr;
	PFN_vkCmdCopyAccelerationStructureKHR CmdCopyAccelerationStructureKHR = nullptr;
	PFN_vkCmdTraceRaysKHR CmdTraceRaysKHR = nullptr;
	PFN_vkGetBufferDeviceAddressKHR GetBufferDeviceAddressKHR = nullptr;

	std::unique_ptr<FVulkanMemoryAllocator> MemoryAllocator;
};

} // namespace Maho
