#pragma once

#include "RHI.h"

#include "VulkanCommandList.h"
#include "VulkanMemory.h"
#include "VulkanResources.h"

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

	[[nodiscard]] virtual FRHICommandList* GetFrameCommandList() override;
	virtual void PresentTexture(FRHITexture* Src) override;
	[[nodiscard]] virtual ERHIFormat GetSwapchainFormat() const override;

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

	// Non-owning RHI views of the swapchain framebuffers + render pass (the
	// swapchain owns the Vk handles; these are only for BeginRenderPass).
	std::vector<FRHIFramebuffer*> SwapchainFramebufferRHI;
	FRHIRenderPass* SwapchainRenderPassRHI = nullptr;

	VkRenderPass RenderPass = VK_NULL_HANDLE;

	/** Frame path command pool (graphics family). */
	VkCommandPool CommandPool = VK_NULL_HANDLE;
	VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;

	/** Non-owning recording surface wrapping CommandBuffer (borrowed by features). */
	FRHICommandList* FrameCommandListRHI = nullptr;

	VkCommandPool GraphicsCmdPool = VK_NULL_HANDLE;
	VkCommandPool ComputeCmdPool = VK_NULL_HANDLE;
	VkCommandPool TransferCmdPool = VK_NULL_HANDLE;

	VkSemaphore ImageAvailableSemaphore = VK_NULL_HANDLE;
	std::vector<VkSemaphore> RenderFinishedSemaphores;   // one per swapchain image (indexed by CurrentImageIndex)
	VkFence InFlightFence = VK_NULL_HANDLE;

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
