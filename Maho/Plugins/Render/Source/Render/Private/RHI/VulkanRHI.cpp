#include "VulkanRHI.h"

#include "VulkanResources.h"

#include <Core/Misc/Console.h>
#include <Core/Misc/Log.h>

#if defined(_WIN32)
#	include <windows.h>
#endif

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <string>

namespace Maho
{

namespace
{

static TAutoConsoleVariable GCVarVSync(
	"r.VSync",
	1,
	"0=prefer Mailbox/Immediate, 1=FIFO (vsync)");

static TAutoConsoleVariable GCVarSwapchainExtraImages(
	"r.Swapchain.ExtraImages",
	1,
	"Extra swapchain images beyond minImageCount (clamped by device max)");

static TAutoConsoleVariable GCVarMinSwapchainImages(
	"r.MinSwapchainImages",
	2,
	"Reported minimum swapchain image count (ImGui / present contract)");

constexpr const char* GInstanceExtensions[] =
{
	VK_KHR_SURFACE_EXTENSION_NAME,
#if defined(_WIN32)
	VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
};

constexpr const char* GDeviceExtensions[] =
{
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
	VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
	VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
};

[[nodiscard]] bool CheckVkResult(VkResult Result, const char* Context)
{
	if (Result == VK_SUCCESS)
	{
		return true;
	}

	MAHO_CORE_ERROR("Vulkan: {} failed with VkResult {}", Context, static_cast<int>(Result));
	return false;
}

} // namespace

FVulkanRHI::FVulkanRHI() = default;

FVulkanRHI::~FVulkanRHI()
{
	Shutdown();
}

bool FVulkanRHI::Initialize(const FRHIInitDesc& Desc)
{
	if (bInitialized)
	{
		MAHO_CORE_ERROR("FVulkanRHI::Initialize: already initialized");
		return false;
	}

	if (Desc.Backend != ERHIBackend::Vulkan)
	{
		MAHO_CORE_ERROR("FVulkanRHI::Initialize: unsupported backend ({})", static_cast<std::uint32_t>(Desc.Backend));
		return false;
	}

	if (Desc.NativeWindowHandle == nullptr)
	{
		MAHO_CORE_ERROR("FVulkanRHI::Initialize: NativeWindowHandle is null");
		return false;
	}

	if (Desc.FramebufferWidth <= 0 || Desc.FramebufferHeight <= 0)
	{
		MAHO_CORE_ERROR("FVulkanRHI::Initialize: invalid framebuffer size {}x{}", Desc.FramebufferWidth, Desc.FramebufferHeight);
		return false;
	}

	NativeWindowHandle = Desc.NativeWindowHandle;
	FramebufferWidth = Desc.FramebufferWidth;
	FramebufferHeight = Desc.FramebufferHeight;

	if (!CreateInstance())
	{
		Shutdown();
		return false;
	}

	if (!CreateSurface())
	{
		Shutdown();
		return false;
	}

	if (!PickPhysicalDevice())
	{
		Shutdown();
		return false;
	}

	if (!CreateLogicalDevice())
	{
		Shutdown();
		return false;
	}

	if (!CreateMemoryAllocatorAndManager())
	{
		Shutdown();
		return false;
	}

	if (!CreateLogicalQueuesAndPools())
	{
		Shutdown();
		return false;
	}

	if (!CreateSwapchain())
	{
		Shutdown();
		return false;
	}

	if (!CreateImageViews())
	{
		Shutdown();
		return false;
	}

	if (!CreateRenderPass())
	{
		Shutdown();
		return false;
	}

	if (!CreateFramebuffers())
	{
		Shutdown();
		return false;
	}

	if (!CreateCommandPoolAndBuffer())
	{
		Shutdown();
		return false;
	}

	if (!CreateSyncObjects())
	{
		Shutdown();
		return false;
	}

	bInitialized = true;
	MAHO_CORE_INFO("FVulkanRHI initialized ({}x{})", FramebufferWidth, FramebufferHeight);
	return true;
}

void FVulkanRHI::Shutdown()
{
	if (Device != VK_NULL_HANDLE)
	{
		vkDeviceWaitIdle(Device);
	}

	if (ResourceManager)
	{
		ResourceManager->Shutdown();
		ResourceManager.reset();
	}

	if (MemoryAllocator)
	{
		MemoryAllocator->Shutdown();
		MemoryAllocator.reset();
	}

	if (Device != VK_NULL_HANDLE && InFlightFence != VK_NULL_HANDLE)
	{
		vkDestroyFence(Device, InFlightFence, nullptr);
		InFlightFence = VK_NULL_HANDLE;
	}

	if (Device != VK_NULL_HANDLE && RenderFinishedSemaphore != VK_NULL_HANDLE)
	{
		vkDestroySemaphore(Device, RenderFinishedSemaphore, nullptr);
		RenderFinishedSemaphore = VK_NULL_HANDLE;
	}

	if (Device != VK_NULL_HANDLE && ImageAvailableSemaphore != VK_NULL_HANDLE)
	{
		vkDestroySemaphore(Device, ImageAvailableSemaphore, nullptr);
		ImageAvailableSemaphore = VK_NULL_HANDLE;
	}

	auto DestroyPool = [this](VkCommandPool& Pool)
	{
		if (Device != VK_NULL_HANDLE && Pool != VK_NULL_HANDLE)
		{
			vkDestroyCommandPool(Device, Pool, nullptr);
			Pool = VK_NULL_HANDLE;
		}
	};

	// Logical pools may alias the same VkCommandPool handle.
	{
		std::set<VkCommandPool> UniqueLogicalPools;
		if (GraphicsCmdPool != VK_NULL_HANDLE)
		{
			UniqueLogicalPools.insert(GraphicsCmdPool);
		}
		if (ComputeCmdPool != VK_NULL_HANDLE)
		{
			UniqueLogicalPools.insert(ComputeCmdPool);
		}
		if (TransferCmdPool != VK_NULL_HANDLE)
		{
			UniqueLogicalPools.insert(TransferCmdPool);
		}

		for (VkCommandPool Pool : UniqueLogicalPools)
		{
			VkCommandPool Temp = Pool;
			DestroyPool(Temp);
		}
		GraphicsCmdPool = VK_NULL_HANDLE;
		ComputeCmdPool = VK_NULL_HANDLE;
		TransferCmdPool = VK_NULL_HANDLE;
	}

	if (Device != VK_NULL_HANDLE && CommandPool != VK_NULL_HANDLE)
	{
		vkDestroyCommandPool(Device, CommandPool, nullptr);
		CommandPool = VK_NULL_HANDLE;
		CommandBuffer = VK_NULL_HANDLE;
	}

	DestroySwapchainResources();

	if (Device != VK_NULL_HANDLE && RenderPass != VK_NULL_HANDLE)
	{
		vkDestroyRenderPass(Device, RenderPass, nullptr);
		RenderPass = VK_NULL_HANDLE;
	}

	if (Device != VK_NULL_HANDLE)
	{
		vkDestroyDevice(Device, nullptr);
		Device = VK_NULL_HANDLE;
		GraphicsVkQueue = VK_NULL_HANDLE;
		PresentQueue = VK_NULL_HANDLE;
		ComputeVkQueue = VK_NULL_HANDLE;
		TransferVkQueue = VK_NULL_HANDLE;
	}

	if (Instance != VK_NULL_HANDLE && Surface != VK_NULL_HANDLE)
	{
		vkDestroySurfaceKHR(Instance, Surface, nullptr);
		Surface = VK_NULL_HANDLE;
	}

	if (Instance != VK_NULL_HANDLE)
	{
		vkDestroyInstance(Instance, nullptr);
		Instance = VK_NULL_HANDLE;
	}

	PhysicalDevice = VK_NULL_HANDLE;
	bInitialized = false;
	bFramebufferResized = false;
	NativeWindowHandle = nullptr;
}

void FVulkanRHI::BeginFrame()
{
	if (!bInitialized)
	{
		MAHO_CORE_ERROR("FVulkanRHI::BeginFrame: not initialized");
		return;
	}

	if (!CheckVkResult(vkWaitForFences(Device, 1, &InFlightFence, VK_TRUE, UINT64_MAX), "vkWaitForFences"))
	{
		return;
	}

	if (!CheckVkResult(vkResetFences(Device, 1, &InFlightFence), "vkResetFences"))
	{
		return;
	}

	VkResult AcquireResult = vkAcquireNextImageKHR(
		Device,
		Swapchain,
		UINT64_MAX,
		ImageAvailableSemaphore,
		VK_NULL_HANDLE,
		&CurrentImageIndex);

	if (AcquireResult == VK_ERROR_OUT_OF_DATE_KHR || bFramebufferResized)
	{
		bFramebufferResized = false;
		if (!RecreateSwapchain())
		{
			return;
		}

		AcquireResult = vkAcquireNextImageKHR(
			Device,
			Swapchain,
			UINT64_MAX,
			ImageAvailableSemaphore,
			VK_NULL_HANDLE,
			&CurrentImageIndex);
	}

	if (AcquireResult == VK_SUBOPTIMAL_KHR)
	{
		MAHO_CORE_INFO("FVulkanRHI::BeginFrame: swapchain suboptimal");
	}
	else if (!CheckVkResult(AcquireResult, "vkAcquireNextImageKHR"))
	{
		return;
	}
}

void FVulkanRHI::Clear(float R, float G, float B, float A)
{
	BeginMainPass(R, G, B, A);
	EndMainPass();
}

void FVulkanRHI::BeginMainPass(float R, float G, float B, float A)
{
	if (!bInitialized)
	{
		MAHO_CORE_ERROR("FVulkanRHI::BeginMainPass: not initialized");
		return;
	}

	ClearColorR = R;
	ClearColorG = G;
	ClearColorB = B;
	ClearColorA = A;

	if (!CheckVkResult(vkResetCommandBuffer(CommandBuffer, 0), "vkResetCommandBuffer"))
	{
		return;
	}

	VkCommandBufferBeginInfo BeginInfo{};
	BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	if (!CheckVkResult(vkBeginCommandBuffer(CommandBuffer, &BeginInfo), "vkBeginCommandBuffer"))
	{
		return;
	}

	VkClearValue ClearValue{};
	ClearValue.color = {{ClearColorR, ClearColorG, ClearColorB, ClearColorA}};

	VkRenderPassBeginInfo RenderPassInfo{};
	RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	RenderPassInfo.renderPass = RenderPass;
	RenderPassInfo.framebuffer = SwapchainFramebuffers[CurrentImageIndex];
	RenderPassInfo.renderArea.offset = {0, 0};
	RenderPassInfo.renderArea.extent = SwapchainExtent;
	RenderPassInfo.clearValueCount = 1;
	RenderPassInfo.pClearValues = &ClearValue;

	vkCmdBeginRenderPass(CommandBuffer, &RenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void FVulkanRHI::EndMainPass()
{
	if (!bInitialized)
	{
		MAHO_CORE_ERROR("FVulkanRHI::EndMainPass: not initialized");
		return;
	}

	vkCmdEndRenderPass(CommandBuffer);

	if (!CheckVkResult(vkEndCommandBuffer(CommandBuffer), "vkEndCommandBuffer"))
	{
		return;
	}
}

void FVulkanRHI::EndFrame()
{
	if (!bInitialized)
	{
		MAHO_CORE_ERROR("FVulkanRHI::EndFrame: not initialized");
		return;
	}

	VkPipelineStageFlags WaitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

	VkSubmitInfo SubmitInfo{};
	SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	SubmitInfo.waitSemaphoreCount = 1;
	SubmitInfo.pWaitSemaphores = &ImageAvailableSemaphore;
	SubmitInfo.pWaitDstStageMask = WaitStages;
	SubmitInfo.commandBufferCount = 1;
	SubmitInfo.pCommandBuffers = &CommandBuffer;
	SubmitInfo.signalSemaphoreCount = 1;
	SubmitInfo.pSignalSemaphores = &RenderFinishedSemaphore;

	if (!CheckVkResult(vkQueueSubmit(GraphicsVkQueue, 1, &SubmitInfo, InFlightFence), "vkQueueSubmit"))
	{
		return;
	}

	VkPresentInfoKHR PresentInfo{};
	PresentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	PresentInfo.waitSemaphoreCount = 1;
	PresentInfo.pWaitSemaphores = &RenderFinishedSemaphore;
	PresentInfo.swapchainCount = 1;
	PresentInfo.pSwapchains = &Swapchain;
	PresentInfo.pImageIndices = &CurrentImageIndex;

	VkResult PresentResult = vkQueuePresentKHR(PresentQueue, &PresentInfo);

	if (PresentResult == VK_ERROR_OUT_OF_DATE_KHR || PresentResult == VK_SUBOPTIMAL_KHR || bFramebufferResized)
	{
		bFramebufferResized = false;
		RecreateSwapchain();
	}
	else if (!CheckVkResult(PresentResult, "vkQueuePresentKHR"))
	{
		return;
	}
}

void FVulkanRHI::Resize(int Width, int Height)
{
	if (Width <= 0 || Height <= 0)
	{
		MAHO_CORE_ERROR("FVulkanRHI::Resize: invalid size {}x{}", Width, Height);
		return;
	}

	FramebufferWidth = Width;
	FramebufferHeight = Height;
	bFramebufferResized = true;

	if (bInitialized)
	{
		RecreateSwapchain();
	}
}

bool FVulkanRHI::IsInitialized() const
{
	return bInitialized;
}

std::uint32_t FVulkanRHI::GetMinImageCount() const
{
	return static_cast<std::uint32_t>((std::max)(1, GCVarMinSwapchainImages.GetValue()));
}

bool FVulkanRHI::CreateInstance()
{
	VkApplicationInfo AppInfo{};
	AppInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	AppInfo.pApplicationName = "Maho";
	AppInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
	AppInfo.pEngineName = "Maho";
	AppInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
	AppInfo.apiVersion = VK_API_VERSION_1_0;

	VkInstanceCreateInfo CreateInfo{};
	CreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	CreateInfo.pApplicationInfo = &AppInfo;
	CreateInfo.enabledExtensionCount = static_cast<std::uint32_t>(std::size(GInstanceExtensions));
	CreateInfo.ppEnabledExtensionNames = GInstanceExtensions;
	CreateInfo.enabledLayerCount = 0;

	if (!CheckVkResult(vkCreateInstance(&CreateInfo, nullptr, &Instance), "vkCreateInstance"))
	{
		return false;
	}

	MAHO_CORE_INFO("Vulkan instance created");
	return true;
}

bool FVulkanRHI::CreateSurface()
{
#if defined(_WIN32)
	VkWin32SurfaceCreateInfoKHR CreateInfo{};
	CreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	CreateInfo.hwnd = static_cast<HWND>(NativeWindowHandle);
	CreateInfo.hinstance = GetModuleHandle(nullptr);

	if (!CheckVkResult(vkCreateWin32SurfaceKHR(Instance, &CreateInfo, nullptr, &Surface), "vkCreateWin32SurfaceKHR"))
	{
		return false;
	}

	MAHO_CORE_INFO("Vulkan Win32 surface created");
	return true;
#else
	MAHO_CORE_ERROR("FVulkanRHI::CreateSurface: unsupported platform");
	return false;
#endif
}

bool FVulkanRHI::FindQueueFamilies(VkPhysicalDevice InPhysicalDevice)
{
	std::uint32_t QueueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(InPhysicalDevice, &QueueFamilyCount, nullptr);

	std::vector<VkQueueFamilyProperties> QueueFamilies(QueueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(InPhysicalDevice, &QueueFamilyCount, QueueFamilies.data());

	std::uint32_t GraphicsFamily = UINT32_MAX;
	std::uint32_t PresentFamily = UINT32_MAX;
	std::uint32_t ComputeFamily = UINT32_MAX;
	std::uint32_t TransferFamily = UINT32_MAX;
	bool bComputeFallback = true;
	bool bTransferFallback = true;

	for (std::uint32_t Index = 0; Index < QueueFamilyCount; ++Index)
	{
		const VkQueueFlags Flags = QueueFamilies[Index].queueFlags;

		if ((Flags & VK_QUEUE_GRAPHICS_BIT) != 0 && GraphicsFamily == UINT32_MAX)
		{
			GraphicsFamily = Index;
		}

		VkBool32 PresentSupport = VK_FALSE;
		vkGetPhysicalDeviceSurfaceSupportKHR(InPhysicalDevice, Index, Surface, &PresentSupport);
		if (PresentSupport == VK_TRUE && PresentFamily == UINT32_MAX)
		{
			PresentFamily = Index;
		}

		if ((Flags & VK_QUEUE_COMPUTE_BIT) != 0
			&& (Flags & VK_QUEUE_GRAPHICS_BIT) == 0
			&& ComputeFamily == UINT32_MAX)
		{
			ComputeFamily = Index;
			bComputeFallback = false;
		}

		if ((Flags & VK_QUEUE_TRANSFER_BIT) != 0
			&& (Flags & VK_QUEUE_GRAPHICS_BIT) == 0
			&& (Flags & VK_QUEUE_COMPUTE_BIT) == 0
			&& TransferFamily == UINT32_MAX)
		{
			TransferFamily = Index;
			bTransferFallback = false;
		}
	}

	if (GraphicsFamily == UINT32_MAX || PresentFamily == UINT32_MAX)
	{
		return false;
	}

	if (ComputeFamily == UINT32_MAX)
	{
		ComputeFamily = GraphicsFamily;
		bComputeFallback = true;
	}

	if (TransferFamily == UINT32_MAX)
	{
		TransferFamily = GraphicsFamily;
		bTransferFallback = true;
	}

	GraphicsQueueFamilyIndex = GraphicsFamily;
	PresentQueueFamilyIndex = PresentFamily;
	ComputeQueueFamilyIndex = ComputeFamily;
	TransferQueueFamilyIndex = TransferFamily;
	bComputeNativeFallback = bComputeFallback;
	bTransferNativeFallback = bTransferFallback;
	return true;
}

bool FVulkanRHI::CheckDeviceExtensionSupport(VkPhysicalDevice InPhysicalDevice) const
{
	std::uint32_t ExtensionCount = 0;
	vkEnumerateDeviceExtensionProperties(InPhysicalDevice, nullptr, &ExtensionCount, nullptr);

	std::vector<VkExtensionProperties> AvailableExtensions(ExtensionCount);
	vkEnumerateDeviceExtensionProperties(InPhysicalDevice, nullptr, &ExtensionCount, AvailableExtensions.data());

	std::set<std::string> RequiredExtensions(
		std::begin(GDeviceExtensions),
		std::end(GDeviceExtensions));

	for (const VkExtensionProperties& Extension : AvailableExtensions)
	{
		RequiredExtensions.erase(Extension.extensionName);
	}

	return RequiredExtensions.empty();
}

bool FVulkanRHI::IsDeviceSuitable(VkPhysicalDevice InPhysicalDevice)
{
	if (!FindQueueFamilies(InPhysicalDevice))
	{
		return false;
	}

	if (!CheckDeviceExtensionSupport(InPhysicalDevice))
	{
		return false;
	}

	VkSurfaceCapabilitiesKHR Capabilities{};
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(InPhysicalDevice, Surface, &Capabilities);

	std::uint32_t FormatCount = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(InPhysicalDevice, Surface, &FormatCount, nullptr);
	if (FormatCount == 0)
	{
		return false;
	}

	std::uint32_t PresentModeCount = 0;
	vkGetPhysicalDeviceSurfacePresentModesKHR(InPhysicalDevice, Surface, &PresentModeCount, nullptr);
	if (PresentModeCount == 0)
	{
		return false;
	}

	return true;
}

bool FVulkanRHI::PickPhysicalDevice()
{
	std::uint32_t DeviceCount = 0;
	vkEnumeratePhysicalDevices(Instance, &DeviceCount, nullptr);
	if (DeviceCount == 0)
	{
		MAHO_CORE_ERROR("FVulkanRHI::PickPhysicalDevice: no Vulkan-capable GPU found");
		return false;
	}

	std::vector<VkPhysicalDevice> Devices(DeviceCount);
	vkEnumeratePhysicalDevices(Instance, &DeviceCount, Devices.data());

	VkPhysicalDevice SelectedDevice = VK_NULL_HANDLE;
	VkPhysicalDevice FallbackIntegrated = VK_NULL_HANDLE;

	for (VkPhysicalDevice CandidateDevice : Devices)
	{
		if (!IsDeviceSuitable(CandidateDevice))
		{
			continue;
		}

		VkPhysicalDeviceProperties Properties{};
		vkGetPhysicalDeviceProperties(CandidateDevice, &Properties);

		if (Properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		{
			SelectedDevice = CandidateDevice;
			MAHO_CORE_INFO("Vulkan physical device selected: {} (discrete)", Properties.deviceName);
			break;
		}

		if (Properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU && FallbackIntegrated == VK_NULL_HANDLE)
		{
			FallbackIntegrated = CandidateDevice;
		}
	}

	if (SelectedDevice == VK_NULL_HANDLE)
	{
		SelectedDevice = FallbackIntegrated;
	}

	if (SelectedDevice == VK_NULL_HANDLE)
	{
		for (VkPhysicalDevice CandidateDevice : Devices)
		{
			if (IsDeviceSuitable(CandidateDevice))
			{
				SelectedDevice = CandidateDevice;
				VkPhysicalDeviceProperties Properties{};
				vkGetPhysicalDeviceProperties(CandidateDevice, &Properties);
				MAHO_CORE_INFO("Vulkan physical device selected: {} (fallback)", Properties.deviceName);
				break;
			}
		}
	}

	if (SelectedDevice == VK_NULL_HANDLE)
	{
		MAHO_CORE_ERROR("FVulkanRHI::PickPhysicalDevice: no suitable GPU found");
		return false;
	}

	PhysicalDevice = SelectedDevice;

	if (!FindQueueFamilies(PhysicalDevice))
	{
		MAHO_CORE_ERROR("FVulkanRHI::PickPhysicalDevice: queue families not found");
		return false;
	}

	return true;
}

bool FVulkanRHI::CreateLogicalDevice()
{
	std::uint32_t QueueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &QueueFamilyCount, nullptr);
	std::vector<VkQueueFamilyProperties> QueueFamilies(QueueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &QueueFamilyCount, QueueFamilies.data());

	std::map<std::uint32_t, std::uint32_t> FamilyQueueCounts;
	auto RequireQueues = [&](std::uint32_t Family, std::uint32_t Count)
	{
		const std::uint32_t MaxCount = QueueFamilies[Family].queueCount;
		const std::uint32_t Clamped = Count > MaxCount ? MaxCount : Count;
		auto It = FamilyQueueCounts.find(Family);
		if (It == FamilyQueueCounts.end() || It->second < Clamped)
		{
			FamilyQueueCounts[Family] = Clamped;
		}
	};

	GraphicsQueueIndex = 0;
	ComputeQueueIndex = 0;
	TransferQueueIndex = 0;

	RequireQueues(GraphicsQueueFamilyIndex, 1);
	RequireQueues(PresentQueueFamilyIndex, 1);

	if (ComputeQueueFamilyIndex == GraphicsQueueFamilyIndex
		&& QueueFamilies[GraphicsQueueFamilyIndex].queueCount >= 2)
	{
		RequireQueues(GraphicsQueueFamilyIndex, 2);
		ComputeQueueIndex = 1;
	}
	else
	{
		RequireQueues(ComputeQueueFamilyIndex, 1);
		ComputeQueueIndex = 0;
	}

	if (TransferQueueFamilyIndex == GraphicsQueueFamilyIndex)
	{
		TransferQueueIndex = 0;
		RequireQueues(GraphicsQueueFamilyIndex, FamilyQueueCounts[GraphicsQueueFamilyIndex]);
	}
	else if (TransferQueueFamilyIndex == ComputeQueueFamilyIndex)
	{
		TransferQueueIndex = ComputeQueueIndex;
		RequireQueues(ComputeQueueFamilyIndex, FamilyQueueCounts[ComputeQueueFamilyIndex]);
	}
	else
	{
		RequireQueues(TransferQueueFamilyIndex, 1);
		TransferQueueIndex = 0;
	}

	std::vector<float> Priorities(4, 1.0f);
	std::vector<VkDeviceQueueCreateInfo> QueueCreateInfos;
	for (const auto& Pair : FamilyQueueCounts)
	{
		VkDeviceQueueCreateInfo QueueCreateInfo{};
		QueueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		QueueCreateInfo.queueFamilyIndex = Pair.first;
		QueueCreateInfo.queueCount = Pair.second;
		QueueCreateInfo.pQueuePriorities = Priorities.data();
		QueueCreateInfos.push_back(QueueCreateInfo);
	}

	VkPhysicalDeviceFeatures DeviceFeatures{};

	VkPhysicalDeviceDynamicRenderingFeatures DynamicRendering{};
	DynamicRendering.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
	DynamicRendering.dynamicRendering = VK_TRUE;

	VkPhysicalDeviceDescriptorIndexingFeatures DescriptorIndexing{};
	DescriptorIndexing.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
	DescriptorIndexing.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
	DescriptorIndexing.descriptorBindingPartiallyBound = VK_TRUE;
	DescriptorIndexing.descriptorBindingVariableDescriptorCount = VK_TRUE;
	DescriptorIndexing.runtimeDescriptorArray = VK_TRUE;

	DynamicRendering.pNext = &DescriptorIndexing;

	VkDeviceCreateInfo CreateInfo{};
	CreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	CreateInfo.pNext = &DynamicRendering;
	CreateInfo.queueCreateInfoCount = static_cast<std::uint32_t>(QueueCreateInfos.size());
	CreateInfo.pQueueCreateInfos = QueueCreateInfos.data();
	CreateInfo.pEnabledFeatures = &DeviceFeatures;
	CreateInfo.enabledExtensionCount = static_cast<std::uint32_t>(std::size(GDeviceExtensions));
	CreateInfo.ppEnabledExtensionNames = GDeviceExtensions;
	CreateInfo.enabledLayerCount = 0;

	if (!CheckVkResult(vkCreateDevice(PhysicalDevice, &CreateInfo, nullptr, &Device), "vkCreateDevice"))
	{
		return false;
	}

	vkGetDeviceQueue(Device, GraphicsQueueFamilyIndex, GraphicsQueueIndex, &GraphicsVkQueue);
	vkGetDeviceQueue(Device, PresentQueueFamilyIndex, 0, &PresentQueue);
	vkGetDeviceQueue(Device, ComputeQueueFamilyIndex, ComputeQueueIndex, &ComputeVkQueue);
	vkGetDeviceQueue(Device, TransferQueueFamilyIndex, TransferQueueIndex, &TransferVkQueue);

	MAHO_CORE_INFO(
		"Vulkan logical device created (G fam={} idx={}, C fam={} idx={} fallback={}, T fam={} idx={} fallback={})",
		GraphicsQueueFamilyIndex,
		GraphicsQueueIndex,
		ComputeQueueFamilyIndex,
		ComputeQueueIndex,
		bComputeNativeFallback,
		TransferQueueFamilyIndex,
		TransferQueueIndex,
		bTransferNativeFallback);

	if (bTransferNativeFallback)
	{
		MAHO_CORE_INFO("Transfer: fallback to Graphics (no dedicated TRANSFER family)");
	}
	else
	{
		MAHO_CORE_INFO("Transfer: dedicated");
	}

	return true;
}

void FVulkanRHI::DestroySwapchainResources()
{
	if (Device != VK_NULL_HANDLE)
	{
		for (VkFramebuffer Framebuffer : SwapchainFramebuffers)
		{
			if (Framebuffer != VK_NULL_HANDLE)
			{
				vkDestroyFramebuffer(Device, Framebuffer, nullptr);
			}
		}
	}

	SwapchainFramebuffers.clear();

	if (Device != VK_NULL_HANDLE)
	{
		for (VkImageView ImageView : SwapchainImageViews)
		{
			if (ImageView != VK_NULL_HANDLE)
			{
				vkDestroyImageView(Device, ImageView, nullptr);
			}
		}
	}

	SwapchainImageViews.clear();
	SwapchainImages.clear();

	if (Device != VK_NULL_HANDLE && Swapchain != VK_NULL_HANDLE)
	{
		vkDestroySwapchainKHR(Device, Swapchain, nullptr);
		Swapchain = VK_NULL_HANDLE;
	}
}

bool FVulkanRHI::CreateSwapchain()
{
	VkSurfaceCapabilitiesKHR Capabilities{};
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(PhysicalDevice, Surface, &Capabilities);

	std::uint32_t FormatCount = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, Surface, &FormatCount, nullptr);
	std::vector<VkSurfaceFormatKHR> Formats(FormatCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, Surface, &FormatCount, Formats.data());

	SwapchainImageFormat = VK_FORMAT_B8G8R8A8_SRGB;
	VkColorSpaceKHR ColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	bool bFormatFound = false;

	for (const VkSurfaceFormatKHR& Format : Formats)
	{
		if (Format.format == VK_FORMAT_B8G8R8A8_SRGB &&
			Format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
		{
			SwapchainImageFormat = Format.format;
			ColorSpace = Format.colorSpace;
			bFormatFound = true;
			break;
		}
	}

	if (!bFormatFound && !Formats.empty())
	{
		SwapchainImageFormat = Formats[0].format;
		ColorSpace = Formats[0].colorSpace;
	}

	std::uint32_t PresentModeCount = 0;
	vkGetPhysicalDeviceSurfacePresentModesKHR(PhysicalDevice, Surface, &PresentModeCount, nullptr);
	std::vector<VkPresentModeKHR> PresentModes(PresentModeCount);
	vkGetPhysicalDeviceSurfacePresentModesKHR(PhysicalDevice, Surface, &PresentModeCount, PresentModes.data());

	VkPresentModeKHR PresentMode = VK_PRESENT_MODE_FIFO_KHR;
	const int VSync = GCVarVSync.GetValue();
	if (VSync == 0)
	{
		for (VkPresentModeKHR Mode : PresentModes)
		{
			if (Mode == VK_PRESENT_MODE_MAILBOX_KHR)
			{
				PresentMode = Mode;
				break;
			}
		}
		if (PresentMode == VK_PRESENT_MODE_FIFO_KHR)
		{
			for (VkPresentModeKHR Mode : PresentModes)
			{
				if (Mode == VK_PRESENT_MODE_IMMEDIATE_KHR)
				{
					PresentMode = Mode;
					break;
				}
			}
		}
	}
	else
	{
		for (VkPresentModeKHR Mode : PresentModes)
		{
			if (Mode == VK_PRESENT_MODE_FIFO_KHR)
			{
				PresentMode = Mode;
				break;
			}
		}
	}

	if (Capabilities.currentExtent.width != UINT32_MAX)
	{
		SwapchainExtent = Capabilities.currentExtent;
	}
	else
	{
		SwapchainExtent.width = static_cast<std::uint32_t>(FramebufferWidth);
		SwapchainExtent.height = static_cast<std::uint32_t>(FramebufferHeight);
		SwapchainExtent.width = std::clamp(
			SwapchainExtent.width,
			Capabilities.minImageExtent.width,
			Capabilities.maxImageExtent.width);
		SwapchainExtent.height = std::clamp(
			SwapchainExtent.height,
			Capabilities.minImageExtent.height,
			Capabilities.maxImageExtent.height);
	}

	const int ExtraImages = (std::max)(0, GCVarSwapchainExtraImages.GetValue());
	std::uint32_t ImageCount = Capabilities.minImageCount + static_cast<std::uint32_t>(ExtraImages);
	if (Capabilities.maxImageCount > 0 && ImageCount > Capabilities.maxImageCount)
	{
		ImageCount = Capabilities.maxImageCount;
	}

	VkSwapchainCreateInfoKHR CreateInfo{};
	CreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	CreateInfo.surface = Surface;
	CreateInfo.minImageCount = ImageCount;
	CreateInfo.imageFormat = SwapchainImageFormat;
	CreateInfo.imageColorSpace = ColorSpace;
	CreateInfo.imageExtent = SwapchainExtent;
	CreateInfo.imageArrayLayers = 1;
	CreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	CreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	CreateInfo.preTransform = Capabilities.currentTransform;
	CreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	CreateInfo.presentMode = PresentMode;
	CreateInfo.clipped = VK_TRUE;
	CreateInfo.oldSwapchain = VK_NULL_HANDLE;

	if (GraphicsQueueFamilyIndex != PresentQueueFamilyIndex)
	{
		std::uint32_t QueueFamilyIndices[] =
		{
			GraphicsQueueFamilyIndex,
			PresentQueueFamilyIndex,
		};
		CreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		CreateInfo.queueFamilyIndexCount = 2;
		CreateInfo.pQueueFamilyIndices = QueueFamilyIndices;
	}

	if (!CheckVkResult(vkCreateSwapchainKHR(Device, &CreateInfo, nullptr, &Swapchain), "vkCreateSwapchainKHR"))
	{
		return false;
	}

	std::uint32_t SwapchainImageCount = 0;
	vkGetSwapchainImagesKHR(Device, Swapchain, &SwapchainImageCount, nullptr);
	SwapchainImages.resize(SwapchainImageCount);
	vkGetSwapchainImagesKHR(Device, Swapchain, &SwapchainImageCount, SwapchainImages.data());

	MAHO_CORE_INFO(
		"Vulkan swapchain created: {}x{}, {} images, format {}",
		SwapchainExtent.width,
		SwapchainExtent.height,
		SwapchainImageCount,
		static_cast<int>(SwapchainImageFormat));

	return true;
}

bool FVulkanRHI::CreateImageViews()
{
	SwapchainImageViews.resize(SwapchainImages.size());

	for (std::size_t Index = 0; Index < SwapchainImages.size(); ++Index)
	{
		VkImageViewCreateInfo CreateInfo{};
		CreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		CreateInfo.image = SwapchainImages[Index];
		CreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		CreateInfo.format = SwapchainImageFormat;
		CreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		CreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		CreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		CreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		CreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		CreateInfo.subresourceRange.baseMipLevel = 0;
		CreateInfo.subresourceRange.levelCount = 1;
		CreateInfo.subresourceRange.baseArrayLayer = 0;
		CreateInfo.subresourceRange.layerCount = 1;

		if (!CheckVkResult(
			vkCreateImageView(Device, &CreateInfo, nullptr, &SwapchainImageViews[Index]),
			"vkCreateImageView"))
		{
			return false;
		}
	}

	MAHO_CORE_INFO("Vulkan swapchain image views created ({})", SwapchainImageViews.size());
	return true;
}

bool FVulkanRHI::CreateRenderPass()
{
	VkAttachmentDescription ColorAttachment{};
	ColorAttachment.format = SwapchainImageFormat;
	ColorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	ColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	ColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	ColorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	ColorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	ColorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	ColorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference ColorAttachmentRef{};
	ColorAttachmentRef.attachment = 0;
	ColorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription Subpass{};
	Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	Subpass.colorAttachmentCount = 1;
	Subpass.pColorAttachments = &ColorAttachmentRef;

	VkSubpassDependency Dependency{};
	Dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	Dependency.dstSubpass = 0;
	Dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	Dependency.srcAccessMask = 0;
	Dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	Dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo RenderPassInfo{};
	RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	RenderPassInfo.attachmentCount = 1;
	RenderPassInfo.pAttachments = &ColorAttachment;
	RenderPassInfo.subpassCount = 1;
	RenderPassInfo.pSubpasses = &Subpass;
	RenderPassInfo.dependencyCount = 1;
	RenderPassInfo.pDependencies = &Dependency;

	if (!CheckVkResult(vkCreateRenderPass(Device, &RenderPassInfo, nullptr, &RenderPass), "vkCreateRenderPass"))
	{
		return false;
	}

	MAHO_CORE_INFO("Vulkan render pass created");
	return true;
}

bool FVulkanRHI::CreateFramebuffers()
{
	SwapchainFramebuffers.resize(SwapchainImageViews.size());

	for (std::size_t Index = 0; Index < SwapchainImageViews.size(); ++Index)
	{
		VkImageView Attachments[] = {SwapchainImageViews[Index]};

		VkFramebufferCreateInfo FramebufferInfo{};
		FramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		FramebufferInfo.renderPass = RenderPass;
		FramebufferInfo.attachmentCount = 1;
		FramebufferInfo.pAttachments = Attachments;
		FramebufferInfo.width = SwapchainExtent.width;
		FramebufferInfo.height = SwapchainExtent.height;
		FramebufferInfo.layers = 1;

		if (!CheckVkResult(
			vkCreateFramebuffer(Device, &FramebufferInfo, nullptr, &SwapchainFramebuffers[Index]),
			"vkCreateFramebuffer"))
		{
			return false;
		}
	}

	MAHO_CORE_INFO("Vulkan framebuffers created ({})", SwapchainFramebuffers.size());
	return true;
}

bool FVulkanRHI::CreateCommandPoolAndBuffer()
{
	VkCommandPoolCreateInfo PoolInfo{};
	PoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	PoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	PoolInfo.queueFamilyIndex = GraphicsQueueFamilyIndex;

	if (!CheckVkResult(vkCreateCommandPool(Device, &PoolInfo, nullptr, &CommandPool), "vkCreateCommandPool"))
	{
		return false;
	}

	VkCommandBufferAllocateInfo AllocateInfo{};
	AllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	AllocateInfo.commandPool = CommandPool;
	AllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	AllocateInfo.commandBufferCount = 1;

	if (!CheckVkResult(vkAllocateCommandBuffers(Device, &AllocateInfo, &CommandBuffer), "vkAllocateCommandBuffers"))
	{
		return false;
	}

	MAHO_CORE_INFO("Vulkan command pool and buffer created");
	return true;
}

bool FVulkanRHI::CreateSyncObjects()
{
	VkSemaphoreCreateInfo SemaphoreInfo{};
	SemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	if (!CheckVkResult(vkCreateSemaphore(Device, &SemaphoreInfo, nullptr, &ImageAvailableSemaphore), "vkCreateSemaphore (image available)"))
	{
		return false;
	}

	if (!CheckVkResult(vkCreateSemaphore(Device, &SemaphoreInfo, nullptr, &RenderFinishedSemaphore), "vkCreateSemaphore (render finished)"))
	{
		return false;
	}

	VkFenceCreateInfo FenceInfo{};
	FenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	FenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	if (!CheckVkResult(vkCreateFence(Device, &FenceInfo, nullptr, &InFlightFence), "vkCreateFence"))
	{
		return false;
	}

	MAHO_CORE_INFO("Vulkan sync objects created");
	return true;
}

bool FVulkanRHI::CreateMemoryAllocatorAndManager()
{
	MemoryAllocator = std::make_unique<FVulkanMemoryAllocator>();
	if (!MemoryAllocator->Initialize(Instance, PhysicalDevice, Device))
	{
		return false;
	}

	ResourceManager = std::make_unique<FRHIResourceManager>(*this);
	return true;
}

bool FVulkanRHI::CreateLogicalQueuesAndPools()
{
	GraphicsQueue.Configure(ERHIQueueType::Graphics, GraphicsVkQueue, GraphicsQueueFamilyIndex, false);
	ComputeQueue.Configure(ERHIQueueType::Compute, ComputeVkQueue, ComputeQueueFamilyIndex, bComputeNativeFallback);
	TransferQueue.Configure(ERHIQueueType::Transfer, TransferVkQueue, TransferQueueFamilyIndex, bTransferNativeFallback);

	auto CreatePool = [this](std::uint32_t Family, VkCommandPool& OutPool, const char* Name) -> bool
	{
		VkCommandPoolCreateInfo PoolInfo{};
		PoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		PoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		PoolInfo.queueFamilyIndex = Family;
		if (!CheckVkResult(vkCreateCommandPool(Device, &PoolInfo, nullptr, &OutPool), Name))
		{
			return false;
		}
		return true;
	};

	if (!CreatePool(GraphicsQueueFamilyIndex, GraphicsCmdPool, "vkCreateCommandPool (graphics logical)"))
	{
		return false;
	}

	if (ComputeQueueFamilyIndex == GraphicsQueueFamilyIndex)
	{
		ComputeCmdPool = GraphicsCmdPool;
	}
	else if (!CreatePool(ComputeQueueFamilyIndex, ComputeCmdPool, "vkCreateCommandPool (compute)"))
	{
		return false;
	}

	if (TransferQueueFamilyIndex == GraphicsQueueFamilyIndex)
	{
		TransferCmdPool = GraphicsCmdPool;
	}
	else if (TransferQueueFamilyIndex == ComputeQueueFamilyIndex)
	{
		TransferCmdPool = ComputeCmdPool;
	}
	else if (!CreatePool(TransferQueueFamilyIndex, TransferCmdPool, "vkCreateCommandPool (transfer)"))
	{
		return false;
	}

	return true;
}

FRHIResourceManager& FVulkanRHI::GetResourceManager()
{
	return *ResourceManager;
}

IRHIMemoryAllocator* FVulkanRHI::GetMemoryAllocator()
{
	return MemoryAllocator.get();
}

FRHIQueue& FVulkanRHI::GetGraphicsQueue()
{
	return GraphicsQueue;
}

FRHIQueue& FVulkanRHI::GetComputeQueue()
{
	return ComputeQueue;
}

FRHIQueue& FVulkanRHI::GetTransferQueue()
{
	return TransferQueue;
}

VkCommandPool FVulkanRHI::GetPoolForType(ERHICommandListType Type) const
{
	switch (Type)
	{
	case ERHICommandListType::Compute:
		return ComputeCmdPool;
	case ERHICommandListType::Transfer:
		return TransferCmdPool;
	case ERHICommandListType::Graphics:
	default:
		return GraphicsCmdPool;
	}
}

FRHICommandList* FVulkanRHI::CreateCommandList(ERHICommandListType Type)
{
	VkCommandPool Pool = GetPoolForType(Type);
	if (Pool == VK_NULL_HANDLE)
	{
		return nullptr;
	}

	VkCommandBufferAllocateInfo AllocateInfo{};
	AllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	AllocateInfo.commandPool = Pool;
	AllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	AllocateInfo.commandBufferCount = 1;

	VkCommandBuffer CmdBuffer = VK_NULL_HANDLE;
	if (!CheckVkResult(vkAllocateCommandBuffers(Device, &AllocateInfo, &CmdBuffer), "vkAllocateCommandBuffers (logical)"))
	{
		return nullptr;
	}

	return new FVulkanCommandList(Type, Device, Pool, CmdBuffer);
}

void FVulkanRHI::DestroyCommandList(FRHICommandList* CmdList)
{
	if (CmdList == nullptr)
	{
		return;
	}

	auto* VulkanCL = static_cast<FVulkanCommandList*>(CmdList);
	VkCommandBuffer Buf = VulkanCL->GetVkCommandBuffer();
	VkCommandPool Pool = VulkanCL->GetVkCommandPool();
	if (Device != VK_NULL_HANDLE && Pool != VK_NULL_HANDLE && Buf != VK_NULL_HANDLE)
	{
		vkFreeCommandBuffers(Device, Pool, 1, &Buf);
	}
	delete VulkanCL;
}

FRHIFence* FVulkanRHI::CreateFence(bool bSignaled)
{
	VkFenceCreateInfo FenceInfo{};
	FenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	FenceInfo.flags = bSignaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;

	VkFence Fence = VK_NULL_HANDLE;
	if (!CheckVkResult(vkCreateFence(Device, &FenceInfo, nullptr, &Fence), "vkCreateFence"))
	{
		return nullptr;
	}
	return new FVulkanFence(Device, Fence);
}

void FVulkanRHI::DestroyFence(FRHIFence* Fence)
{
	delete Fence;
}

void FVulkanRHI::WaitForFence(FRHIFence* Fence, std::uint64_t TimeoutNs)
{
	if (Fence == nullptr)
	{
		return;
	}
	VkFence Handle = static_cast<FVulkanFence*>(Fence)->GetVkFence();
	vkWaitForFences(Device, 1, &Handle, VK_TRUE, TimeoutNs);
}

bool FVulkanRHI::IsFenceSignaled(FRHIFence* Fence)
{
	if (Fence == nullptr)
	{
		return false;
	}
	VkFence Handle = static_cast<FVulkanFence*>(Fence)->GetVkFence();
	return vkGetFenceStatus(Device, Handle) == VK_SUCCESS;
}

FRHISemaphore* FVulkanRHI::CreateGpuSemaphore()
{
	VkSemaphoreCreateInfo Info{};
	Info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	VkSemaphore Semaphore = VK_NULL_HANDLE;
	if (!CheckVkResult(vkCreateSemaphore(Device, &Info, nullptr, &Semaphore), "vkCreateSemaphore"))
	{
		return nullptr;
	}
	return new FVulkanSemaphore(Device, Semaphore);
}

void FVulkanRHI::DestroyGpuSemaphore(FRHISemaphore* Semaphore)
{
	delete Semaphore;
}

void FVulkanRHI::UpdateBuffer(FRHIBuffer* Buffer, std::uint64_t Offset, std::uint64_t Size, const void* Data)
{
	auto* VulkanBuffer = static_cast<FVulkanBuffer*>(Buffer);
	if (VulkanBuffer == nullptr || Data == nullptr || MemoryAllocator == nullptr || !MemoryAllocator->IsValid())
	{
		return;
	}

	FRHIMemoryAllocation Opaque{};
	Opaque.Native = VulkanBuffer->GetAllocation();
	void* Mapped = MemoryAllocator->Map(Opaque);
	if (Mapped == nullptr)
	{
		MAHO_CORE_ERROR("FVulkanRHI::UpdateBuffer: map failed");
		return;
	}

	std::memcpy(static_cast<std::uint8_t*>(Mapped) + Offset, Data, static_cast<std::size_t>(Size));
	MemoryAllocator->Unmap(Opaque);
}

void FVulkanRHI::UpdateDescriptorSets(const FRHIDescriptorWrite* Writes, std::uint32_t Count)
{
	if (Writes == nullptr || Count == 0 || Device == VK_NULL_HANDLE)
	{
		return;
	}

	std::vector<VkWriteDescriptorSet> VkWrites;
	VkWrites.reserve(Count);

	// Temporary storage for descriptor info structs (must live until vkUpdateDescriptorSets call)
	std::vector<VkDescriptorBufferInfo> BufferInfos;
	BufferInfos.reserve(Count);
	std::vector<VkDescriptorImageInfo> ImageInfos;
	ImageInfos.reserve(Count);

	for (std::uint32_t i = 0; i < Count; ++i)
	{
		const FRHIDescriptorWrite& W = Writes[i];
		if (W.Set == nullptr)
		{
			continue;
		}

		auto* VkSet = static_cast<FVulkanDescriptorSet*>(W.Set);

		VkWriteDescriptorSet VkWrite{};
		VkWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		VkWrite.dstSet = VkSet->GetVkSet();
		VkWrite.dstBinding = W.Binding;
		VkWrite.dstArrayElement = W.ArrayIndex;
		VkWrite.descriptorCount = 1;
		VkWrite.descriptorType = ToVkDescriptorType(W.Type);

		bool bHasInfo = false;
		if (W.Type == ERHIDescriptorType::UniformBuffer && W.Buffer != nullptr)
		{
			auto* VkBuf = static_cast<FVulkanBuffer*>(W.Buffer);
			VkDescriptorBufferInfo& Info = BufferInfos.emplace_back();
			Info.buffer = VkBuf->GetVkBuffer();
			Info.offset = W.Offset;
			Info.range = W.Range > 0 ? W.Range : VK_WHOLE_SIZE;

			VkWrite.pBufferInfo = &BufferInfos.back();
			bHasInfo = (Info.buffer != VK_NULL_HANDLE);
		}
		else if (W.Type == ERHIDescriptorType::CombinedImageSampler && W.TextureView != nullptr)
		{
			auto* VkView = static_cast<FVulkanTextureView*>(W.TextureView);
			auto* VkSampler = (W.Sampler != nullptr) ? static_cast<FVulkanSampler*>(W.Sampler) : nullptr;

			VkDescriptorImageInfo& Info = ImageInfos.emplace_back();
			Info.imageView = VkView->GetVkImageView();
			Info.sampler = VkSampler ? VkSampler->GetVkSampler() : VK_NULL_HANDLE;
			Info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			VkWrite.pImageInfo = &ImageInfos.back();
			bHasInfo = (Info.imageView != VK_NULL_HANDLE);
		}

		if (bHasInfo)
		{
			VkWrites.push_back(VkWrite);
		}
	}

	if (!VkWrites.empty())
	{
		vkUpdateDescriptorSets(Device, static_cast<std::uint32_t>(VkWrites.size()),
		                       VkWrites.data(), 0, nullptr);
	}
}

VkBufferUsageFlags FVulkanRHI::ToVkBufferUsage(ERHIBufferUsage Usage)
{
	VkBufferUsageFlags Flags = 0;
	if (RHIEnumHas(Usage, ERHIBufferUsage::Vertex))
	{
		Flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	}
	if (RHIEnumHas(Usage, ERHIBufferUsage::Index))
	{
		Flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	}
	if (RHIEnumHas(Usage, ERHIBufferUsage::Uniform))
	{
		Flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	}
	if (RHIEnumHas(Usage, ERHIBufferUsage::Storage))
	{
		Flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	}
	if (RHIEnumHas(Usage, ERHIBufferUsage::TransferSrc))
	{
		Flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	}
	if (RHIEnumHas(Usage, ERHIBufferUsage::TransferDst))
	{
		Flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	}
	if (RHIEnumHas(Usage, ERHIBufferUsage::Indirect))
	{
		Flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
	}
	return Flags;
}

VkImageUsageFlags FVulkanRHI::ToVkImageUsage(ERHITextureUsage Usage)
{
	VkImageUsageFlags Flags = 0;
	if (RHIEnumHas(Usage, ERHITextureUsage::Sampled))
	{
		Flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
	}
	if (RHIEnumHas(Usage, ERHITextureUsage::ColorAttachment))
	{
		Flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}
	if (RHIEnumHas(Usage, ERHITextureUsage::DepthStencil))
	{
		Flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	}
	if (RHIEnumHas(Usage, ERHITextureUsage::Storage))
	{
		Flags |= VK_IMAGE_USAGE_STORAGE_BIT;
	}
	if (RHIEnumHas(Usage, ERHITextureUsage::TransferSrc))
	{
		Flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}
	if (RHIEnumHas(Usage, ERHITextureUsage::TransferDst))
	{
		Flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	}
	if (RHIEnumHas(Usage, ERHITextureUsage::Transient))
	{
		Flags |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
	}
	return Flags;
}

VkFormat FVulkanRHI::ToVkFormat(ERHIFormat Format)
{
	switch (Format)
	{
	case ERHIFormat::R8G8B8A8_UNORM:
		return VK_FORMAT_R8G8B8A8_UNORM;
	case ERHIFormat::B8G8R8A8_UNORM:
		return VK_FORMAT_B8G8R8A8_UNORM;
	case ERHIFormat::R32_SFLOAT:
		return VK_FORMAT_R32_SFLOAT;
	case ERHIFormat::R32G32_SFLOAT:
		return VK_FORMAT_R32G32_SFLOAT;
	case ERHIFormat::R32G32B32_SFLOAT:
		return VK_FORMAT_R32G32B32_SFLOAT;
	case ERHIFormat::R16G16_SFLOAT:
		return VK_FORMAT_R16G16_SFLOAT;
	case ERHIFormat::D24_UNORM_S8_UINT:
		return VK_FORMAT_D24_UNORM_S8_UINT;
	case ERHIFormat::D32_SFLOAT:
		return VK_FORMAT_D32_SFLOAT;
	default:
		return VK_FORMAT_UNDEFINED;
	}
}

VkDescriptorType FVulkanRHI::ToVkDescriptorType(ERHIDescriptorType Type)
{
	// ERHIDescriptorType is dense and skips Vulkan texel-buffer enums — never static_cast.
	switch (Type)
	{
	case ERHIDescriptorType::Sampler:
		return VK_DESCRIPTOR_TYPE_SAMPLER;
	case ERHIDescriptorType::CombinedImageSampler:
		return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	case ERHIDescriptorType::SampledImage:
		return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	case ERHIDescriptorType::StorageImage:
		return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	case ERHIDescriptorType::UniformBuffer:
		return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	case ERHIDescriptorType::StorageBuffer:
		return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	case ERHIDescriptorType::DynamicUniform:
		return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	case ERHIDescriptorType::DynamicStorage:
		return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
	default:
		return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	}
}

VkFilter FVulkanRHI::ToVkFilter(ERHIFilter Filter)
{
	return Filter == ERHIFilter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

VkSamplerAddressMode FVulkanRHI::ToVkAddressMode(ERHIAddressMode Mode)
{
	switch (Mode)
	{
	case ERHIAddressMode::MirroredRepeat:
		return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	case ERHIAddressMode::ClampToEdge:
		return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	case ERHIAddressMode::ClampToBorder:
		return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	case ERHIAddressMode::Repeat:
	default:
		return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	}
}

FRHIBuffer* FVulkanRHI::CreateBuffer(const FRHIBufferDesc& Desc)
{
	if (MemoryAllocator == nullptr || !MemoryAllocator->IsValid() || Desc.Size == 0)
	{
		return nullptr;
	}

	VkBufferCreateInfo BufferInfo{};
	BufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	BufferInfo.size = Desc.Size;
	BufferInfo.usage = ToVkBufferUsage(Desc.Usage);
	BufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo AllocInfo = FVulkanMemoryAllocator::MakeAllocationInfo(Desc.MemoryUsage);
	VkBuffer Buffer = VK_NULL_HANDLE;
	VmaAllocation Allocation = nullptr;
	if (!MemoryAllocator->CreateBuffer(BufferInfo, AllocInfo, Buffer, Allocation))
	{
		return nullptr;
	}

	return new FVulkanBuffer(Desc, Buffer, Allocation, MemoryAllocator.get());
}

void FVulkanRHI::DestroyBuffer(FRHIBuffer* Buffer)
{
	delete Buffer;
}

FRHITexture* FVulkanRHI::CreateTexture(const FRHITextureDesc& Desc)
{
	if (MemoryAllocator == nullptr || !MemoryAllocator->IsValid())
	{
		return nullptr;
	}

	VkImageCreateInfo ImageInfo{};
	ImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ImageInfo.imageType = VK_IMAGE_TYPE_2D;
	ImageInfo.format = ToVkFormat(Desc.Format);
	ImageInfo.extent = { Desc.Extent.Width, Desc.Extent.Height, Desc.Extent.Depth };
	ImageInfo.mipLevels = Desc.MipLevels;
	ImageInfo.arrayLayers = Desc.ArrayLayers;
	ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	ImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	ImageInfo.usage = ToVkImageUsage(Desc.Usage);
	ImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	ImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VmaAllocationCreateInfo AllocInfo = FVulkanMemoryAllocator::MakeAllocationInfo(Desc.MemoryUsage);
	VkImage Image = VK_NULL_HANDLE;
	VmaAllocation Allocation = nullptr;
	if (!MemoryAllocator->CreateImage(ImageInfo, AllocInfo, Image, Allocation))
	{
		return nullptr;
	}

	return new FVulkanTexture(Desc, Image, Allocation, MemoryAllocator.get());
}

void FVulkanRHI::DestroyTexture(FRHITexture* Texture)
{
	delete Texture;
}

FRHISampler* FVulkanRHI::CreateSampler(const FRHISamplerDesc& Desc)
{
	VkSamplerCreateInfo Info{};
	Info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	Info.magFilter = ToVkFilter(Desc.MagFilter);
	Info.minFilter = ToVkFilter(Desc.MinFilter);
	Info.addressModeU = ToVkAddressMode(Desc.AddressU);
	Info.addressModeV = ToVkAddressMode(Desc.AddressV);
	Info.addressModeW = ToVkAddressMode(Desc.AddressW);
	Info.mipLodBias = Desc.LodBias;
	Info.minLod = Desc.MinLod;
	Info.maxLod = Desc.MaxLod;

	VkSampler Sampler = VK_NULL_HANDLE;
	if (!CheckVkResult(vkCreateSampler(Device, &Info, nullptr, &Sampler), "vkCreateSampler"))
	{
		return nullptr;
	}
	return new FVulkanSampler(Desc, Device, Sampler);
}

void FVulkanRHI::DestroySampler(FRHISampler* Sampler)
{
	delete Sampler;
}

FRHIShaderModule* FVulkanRHI::CreateShaderModule(const FRHIShaderModuleDesc& Desc)
{
	if (Desc.Bytecode == nullptr || Desc.BytecodeSize == 0)
	{
		return nullptr;
	}

	VkShaderModuleCreateInfo Info{};
	Info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	Info.codeSize = Desc.BytecodeSize;
	Info.pCode = static_cast<const std::uint32_t*>(Desc.Bytecode);

	VkShaderModule Module = VK_NULL_HANDLE;
	if (!CheckVkResult(vkCreateShaderModule(Device, &Info, nullptr, &Module), "vkCreateShaderModule"))
	{
		return nullptr;
	}
	return new FVulkanShaderModule(Device, Module);
}

void FVulkanRHI::DestroyShaderModule(FRHIShaderModule* Module)
{
	delete Module;
}

FRHIGraphicsPipeline* FVulkanRHI::CreateGraphicsPipeline(const FRHIGraphicsPipelineDesc& Desc)
{
	if (Desc.VertexShader == nullptr || Desc.FragmentShader == nullptr)
	{
		MAHO_CORE_ERROR("FVulkanRHI::CreateGraphicsPipeline: missing shader modules");
		return nullptr;
	}

	auto* VkVs = static_cast<FVulkanShaderModule*>(Desc.VertexShader);
	auto* VkFs = static_cast<FVulkanShaderModule*>(Desc.FragmentShader);

	if (VkVs->GetVkShaderModule() == VK_NULL_HANDLE || VkFs->GetVkShaderModule() == VK_NULL_HANDLE)
	{
		MAHO_CORE_ERROR("FVulkanRHI::CreateGraphicsPipeline: invalid shader modules");
		return nullptr;
	}

	VkRenderPass VkRp = VK_NULL_HANDLE;
	VkPipelineRenderingCreateInfo RenderingInfo{};
	if (Desc.RenderPass != nullptr)
	{
		auto* VkPass = static_cast<FVulkanRenderPass*>(Desc.RenderPass);
		VkRp = VkPass->GetVkPass();
	}
	else
	{
		// Dynamic rendering — declare attachment formats via pNext
		RenderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		RenderingInfo.colorAttachmentCount = 1;
		VkFormat ColorFmt = ToVkFormat(Desc.ColorFormat);
		RenderingInfo.pColorAttachmentFormats = &ColorFmt;
		if (Desc.DepthFormat != ERHIFormat::Unknown)
		{
			VkFormat DepthFmt = ToVkFormat(Desc.DepthFormat);
			RenderingInfo.depthAttachmentFormat = DepthFmt;
		}
	}

	VkPipelineLayout VkPLayout = VK_NULL_HANDLE;
	if (Desc.Layout != nullptr)
	{
		auto* VkL = static_cast<FVulkanPipelineLayout*>(Desc.Layout);
		VkPLayout = VkL->GetVkLayout();
	}

	std::vector<VkPipelineShaderStageCreateInfo> ShaderStages;

	VkPipelineShaderStageCreateInfo VsInfo{};
	VsInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	VsInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	VsInfo.module = VkVs->GetVkShaderModule();
	VsInfo.pName = Desc.VertexEntryPoint;
	ShaderStages.push_back(VsInfo);

	VkPipelineShaderStageCreateInfo FsInfo{};
	FsInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	FsInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	FsInfo.module = VkFs->GetVkShaderModule();
	FsInfo.pName = Desc.FragmentEntryPoint;
	ShaderStages.push_back(FsInfo);

	// Vertex input
	std::vector<VkVertexInputBindingDescription> Bindings;
	std::vector<VkVertexInputAttributeDescription> Attrs;
	if (Desc.VertexStride > 0)
	{
		VkVertexInputBindingDescription Bind{};
		Bind.binding = 0;
		Bind.stride = Desc.VertexStride;
		Bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		Bindings.push_back(Bind);

		for (const auto& A : Desc.Attributes)
		{
			VkVertexInputAttributeDescription Attr{};
			Attr.location = A.Location;
			Attr.binding = 0;
			Attr.format = ToVkFormat(A.Format);
			Attr.offset = A.Offset;
			Attrs.push_back(Attr);
		}
	}

	VkPipelineVertexInputStateCreateInfo VertexInput{};
	VertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	VertexInput.vertexBindingDescriptionCount = static_cast<std::uint32_t>(Bindings.size());
	VertexInput.pVertexBindingDescriptions = Bindings.data();
	VertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(Attrs.size());
	VertexInput.pVertexAttributeDescriptions = Attrs.data();

	// Input assembly
	VkPrimitiveTopology VkTopo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	switch (Desc.Topology)
	{
	case ERHIPrimitiveTopology::TriangleStrip:
		VkTopo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		break;
	case ERHIPrimitiveTopology::LineList:
		VkTopo = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		break;
	case ERHIPrimitiveTopology::PointList:
		VkTopo = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
		break;
	default:
		break;
	}

	VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
	InputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	InputAssembly.topology = VkTopo;
	InputAssembly.primitiveRestartEnable = VK_FALSE;

	// Viewport (dynamic)
	VkPipelineViewportStateCreateInfo ViewportState{};
	ViewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	ViewportState.viewportCount = 1;
	ViewportState.scissorCount = 1;

	// Rasterization
	VkCullModeFlags VkCull = VK_CULL_MODE_BACK_BIT;
	if (Desc.CullMode == ERHICullMode::None)
	{
		VkCull = VK_CULL_MODE_NONE;
	}
	else if (Desc.CullMode == ERHICullMode::Front)
	{
		VkCull = VK_CULL_MODE_FRONT_BIT;
	}

	VkPolygonMode VkFill = Desc.FillMode == ERHIFillMode::Wireframe
		? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;

	VkPipelineRasterizationStateCreateInfo Rasterizer{};
	Rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	Rasterizer.depthClampEnable = VK_FALSE;
	Rasterizer.rasterizerDiscardEnable = VK_FALSE;
	Rasterizer.polygonMode = VkFill;
	Rasterizer.cullMode = VkCull;
	Rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	Rasterizer.depthBiasEnable = VK_FALSE;
	Rasterizer.lineWidth = 1.0f;

	// Multisampling
	VkPipelineMultisampleStateCreateInfo Multisampling{};
	Multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	Multisampling.rasterizationSamples = static_cast<VkSampleCountFlagBits>(Desc.SampleCount);
	Multisampling.sampleShadingEnable = VK_FALSE;

	// Depth / stencil
	VkPipelineDepthStencilStateCreateInfo DepthStencil{};
	DepthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	DepthStencil.depthTestEnable = Desc.bDepthTest ? VK_TRUE : VK_FALSE;
	DepthStencil.depthWriteEnable = Desc.bDepthWrite ? VK_TRUE : VK_FALSE;
	switch (Desc.DepthCompare)
	{
	case ERHICompareOp::Never:
		DepthStencil.depthCompareOp = VK_COMPARE_OP_NEVER;
		break;
	case ERHICompareOp::Less:
		DepthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
		break;
	case ERHICompareOp::Equal:
		DepthStencil.depthCompareOp = VK_COMPARE_OP_EQUAL;
		break;
	case ERHICompareOp::LessOrEqual:
		DepthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		break;
	case ERHICompareOp::Greater:
		DepthStencil.depthCompareOp = VK_COMPARE_OP_GREATER;
		break;
	case ERHICompareOp::NotEqual:
		DepthStencil.depthCompareOp = VK_COMPARE_OP_NOT_EQUAL;
		break;
	case ERHICompareOp::GreaterOrEqual:
		DepthStencil.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
		break;
	case ERHICompareOp::Always:
		DepthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
		break;
	}
	DepthStencil.depthBoundsTestEnable = VK_FALSE;
	DepthStencil.stencilTestEnable = VK_FALSE;

	// Color blend
	std::vector<VkPipelineColorBlendAttachmentState> BlendAttachments;
	for (const auto& Blend : Desc.AttachmentBlends)
	{
		VkPipelineColorBlendAttachmentState Attachment{};
		Attachment.blendEnable = Blend.bBlend ? VK_TRUE : VK_FALSE;
		Attachment.srcColorBlendFactor = static_cast<VkBlendFactor>(Blend.SrcColorFactor);
		Attachment.dstColorBlendFactor = static_cast<VkBlendFactor>(Blend.DstColorFactor);
		Attachment.colorBlendOp = static_cast<VkBlendOp>(Blend.ColorOp);
		Attachment.srcAlphaBlendFactor = static_cast<VkBlendFactor>(Blend.SrcAlphaFactor);
		Attachment.dstAlphaBlendFactor = static_cast<VkBlendFactor>(Blend.DstAlphaFactor);
		Attachment.alphaBlendOp = static_cast<VkBlendOp>(Blend.AlphaOp);
		Attachment.colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		BlendAttachments.push_back(Attachment);
	}

	if (BlendAttachments.empty())
	{
		VkPipelineColorBlendAttachmentState Default{};
		Default.colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		Default.blendEnable = VK_FALSE;
		BlendAttachments.push_back(Default);
	}

	VkPipelineColorBlendStateCreateInfo ColorBlend{};
	ColorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	ColorBlend.logicOpEnable = VK_FALSE;
	ColorBlend.attachmentCount = static_cast<std::uint32_t>(BlendAttachments.size());
	ColorBlend.pAttachments = BlendAttachments.data();

	// Dynamic states
	VkDynamicState DynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo DynamicState{};
	DynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	DynamicState.dynamicStateCount = 2;
	DynamicState.pDynamicStates = DynamicStates;

	// Create the pipeline
	VkGraphicsPipelineCreateInfo PipelineInfo{};
	PipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	PipelineInfo.stageCount = static_cast<std::uint32_t>(ShaderStages.size());
	PipelineInfo.pStages = ShaderStages.data();
	PipelineInfo.pVertexInputState = &VertexInput;
	PipelineInfo.pInputAssemblyState = &InputAssembly;
	PipelineInfo.pViewportState = &ViewportState;
	PipelineInfo.pRasterizationState = &Rasterizer;
	PipelineInfo.pMultisampleState = &Multisampling;
	PipelineInfo.pDepthStencilState = &DepthStencil;
	PipelineInfo.pColorBlendState = &ColorBlend;
	PipelineInfo.pDynamicState = &DynamicState;
	PipelineInfo.layout = VkPLayout;
	PipelineInfo.renderPass = VkRp;
	PipelineInfo.subpass = 0;
	PipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

	// Use dynamic rendering pNext when no explicit render pass
	if (VkRp == VK_NULL_HANDLE)
	{
		PipelineInfo.pNext = &RenderingInfo;
	}

	VkPipeline Pipeline = VK_NULL_HANDLE;
	if (!CheckVkResult(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &PipelineInfo, nullptr, &Pipeline),
	                   "vkCreateGraphicsPipelines"))
	{
		return nullptr;
	}

	MAHO_CORE_INFO("FVulkanRHI: created graphics pipeline");
	return new FVulkanGraphicsPipeline(Device, Pipeline, VkPLayout);
}

void FVulkanRHI::DestroyGraphicsPipeline(FRHIGraphicsPipeline* Pipeline)
{
	delete Pipeline;
}

FRHIComputePipeline* FVulkanRHI::CreateComputePipeline(const FRHIComputePipelineDesc& Desc)
{
	if (Desc.ComputeShader == nullptr)
	{
		MAHO_CORE_ERROR("FVulkanRHI::CreateComputePipeline: missing compute shader");
		return nullptr;
	}

	auto* VkCs = static_cast<FVulkanShaderModule*>(Desc.ComputeShader);
	if (VkCs->GetVkShaderModule() == VK_NULL_HANDLE)
	{
		MAHO_CORE_ERROR("FVulkanRHI::CreateComputePipeline: invalid compute shader");
		return nullptr;
	}

	VkPipelineLayout VkPLayout = VK_NULL_HANDLE;
	if (Desc.Layout != nullptr)
	{
		auto* VkL = static_cast<FVulkanPipelineLayout*>(Desc.Layout);
		VkPLayout = VkL->GetVkLayout();
	}

	VkPipelineShaderStageCreateInfo StageInfo{};
	StageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	StageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	StageInfo.module = VkCs->GetVkShaderModule();
	StageInfo.pName = Desc.ComputeEntryPoint;

	VkComputePipelineCreateInfo PipelineInfo{};
	PipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	PipelineInfo.stage = StageInfo;
	PipelineInfo.layout = VkPLayout;
	PipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

	VkPipeline Pipeline = VK_NULL_HANDLE;
	if (!CheckVkResult(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &PipelineInfo, nullptr, &Pipeline),
	                   "vkCreateComputePipelines"))
	{
		return nullptr;
	}

	MAHO_CORE_INFO("FVulkanRHI: created compute pipeline");
	return new FVulkanComputePipeline(Device, Pipeline, VkPLayout);
}

void FVulkanRHI::DestroyComputePipeline(FRHIComputePipeline* Pipeline)
{
	delete Pipeline;
}

FRHIStructuredBuffer* FVulkanRHI::CreateStructuredBuffer(const FRHIStructuredBufferDesc& Desc)
{
	if (Desc.Stride == 0 || Desc.Size == 0)
	{
		MAHO_CORE_ERROR("FVulkanRHI::CreateStructuredBuffer: invalid stride or size");
		return nullptr;
	}

	FRHIBufferDesc BufDesc;
	BufDesc.Size = Desc.Size;
	BufDesc.Usage = Desc.Usage;
	BufDesc.MemoryUsage = Desc.MemoryUsage;
	FRHIBuffer* Underlying = CreateBuffer(BufDesc);
	if (Underlying == nullptr)
	{
		MAHO_CORE_ERROR("FVulkanRHI::CreateStructuredBuffer: failed to create underlying buffer");
		return nullptr;
	}

	auto* Result = new FVulkanStructuredBuffer(Desc, static_cast<FVulkanBuffer*>(Underlying));
	return Result;
}

void FVulkanRHI::DestroyStructuredBuffer(FRHIStructuredBuffer* Buffer)
{
	if (Buffer == nullptr)
	{
		return;
	}
	// Destroy the underlying buffer first.
	FRHIBuffer* Underlying = Buffer->GetUnderlyingBuffer();
	delete Buffer;
	delete Underlying;
}

FRHIBufferView* FVulkanRHI::CreateBufferView(const FRHIBufferViewDesc& Desc)
{
	if (Desc.Buffer == nullptr)
	{
		MAHO_CORE_ERROR("FVulkanRHI::CreateBufferView: null buffer");
		return nullptr;
	}

	auto* Buf = static_cast<FVulkanBuffer*>(Desc.Buffer);
	VkFormat VkFmt = ToVkFormat(Desc.Format);
	if (VkFmt == VK_FORMAT_UNDEFINED)
	{
		MAHO_CORE_ERROR("FVulkanRHI::CreateBufferView: unsupported format");
		return nullptr;
	}

	VkBufferViewCreateInfo ViewInfo{};
	ViewInfo.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
	ViewInfo.buffer = Buf->GetVkBuffer();
	ViewInfo.format = VkFmt;
	ViewInfo.offset = Desc.Offset;
	ViewInfo.range = (Desc.Range == 0) ? VK_WHOLE_SIZE : Desc.Range;

	VkBufferView View = VK_NULL_HANDLE;
	if (!CheckVkResult(vkCreateBufferView(Device, &ViewInfo, nullptr, &View), "vkCreateBufferView"))
	{
		return nullptr;
	}

	return new FVulkanBufferView(Device, View);
}

void FVulkanRHI::DestroyBufferView(FRHIBufferView* View)
{
	delete View;
}

FRHITextureView* FVulkanRHI::CreateTextureView(const FRHITextureViewDesc& Desc)
{
	if (Desc.Texture == nullptr)
	{
		return nullptr;
	}

	auto* VkTex = static_cast<FVulkanTexture*>(Desc.Texture);

	VkImageViewCreateInfo Info{};
	Info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	Info.image = VkTex->GetVkImage();
	Info.format = ToVkFormat(Desc.Format);
	Info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	Info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	Info.subresourceRange.baseMipLevel = Desc.BaseMip;
	Info.subresourceRange.levelCount = Desc.MipCount;
	Info.subresourceRange.baseArrayLayer = Desc.BaseArrayLayer;
	Info.subresourceRange.layerCount = Desc.ArrayLayerCount;

	VkImageView View = VK_NULL_HANDLE;
	if (!CheckVkResult(vkCreateImageView(Device, &Info, nullptr, &View), "vkCreateImageView"))
	{
		return nullptr;
	}

	return new FVulkanTextureView(Device, View);
}

void FVulkanRHI::DestroyTextureView(FRHITextureView* View)
{
	delete View;
}

FRHIDescriptorSetLayout* FVulkanRHI::CreateDescriptorSetLayout(const FRHIDescriptorSetLayoutDesc& Desc)
{
	std::vector<VkDescriptorSetLayoutBinding> Bindings;
	std::vector<VkDescriptorBindingFlags> BindingFlags;
	bool bAnyBindless = false;

	for (const auto& B : Desc.Bindings)
	{
		VkDescriptorSetLayoutBinding Binding{};
		Binding.binding = B.Binding;
		Binding.descriptorType = ToVkDescriptorType(B.Type);
		Binding.descriptorCount = B.Count;

		VkShaderStageFlags Flags = 0;
		if (RHIEnumHas(B.Stages, ERHIShaderStage::Vertex))
		{
			Flags |= VK_SHADER_STAGE_VERTEX_BIT;
		}
		if (RHIEnumHas(B.Stages, ERHIShaderStage::Fragment))
		{
			Flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
		}
		if (RHIEnumHas(B.Stages, ERHIShaderStage::Compute))
		{
			Flags |= VK_SHADER_STAGE_COMPUTE_BIT;
		}
		Binding.stageFlags = Flags;

		Bindings.push_back(Binding);

		VkDescriptorBindingFlags Flags2 = 0;
		if (B.bPartiallyBound)
		{
			Flags2 |= VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
			bAnyBindless = true;
		}
		if (B.bVariableCount)
		{
			Flags2 |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
			bAnyBindless = true;
		}
		BindingFlags.push_back(Flags2);
	}

	VkDescriptorSetLayoutBindingFlagsCreateInfo FlagsInfo{};
	FlagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
	FlagsInfo.bindingCount = static_cast<std::uint32_t>(BindingFlags.size());
	FlagsInfo.pBindingFlags = BindingFlags.data();

	VkDescriptorSetLayoutCreateInfo Info{};
	Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	if (bAnyBindless)
	{
		Info.pNext = &FlagsInfo;
	}
	Info.bindingCount = static_cast<std::uint32_t>(Bindings.size());
	Info.pBindings = Bindings.data();

	VkDescriptorSetLayout Layout = VK_NULL_HANDLE;
	if (!CheckVkResult(vkCreateDescriptorSetLayout(Device, &Info, nullptr, &Layout),
	                   "vkCreateDescriptorSetLayout"))
	{
		return nullptr;
	}

	return new FVulkanDescriptorSetLayout(Device, Layout);
}

void FVulkanRHI::DestroyDescriptorSetLayout(FRHIDescriptorSetLayout* Layout)
{
	delete Layout;
}

FRHIPipelineLayout* FVulkanRHI::CreatePipelineLayout(const FRHIPipelineLayoutDesc& Desc)
{
	std::vector<VkDescriptorSetLayout> SetLayouts;
	for (auto* SetLayout : Desc.SetLayouts)
	{
		if (SetLayout != nullptr)
		{
			auto* VkSl = static_cast<FVulkanDescriptorSetLayout*>(SetLayout);
			SetLayouts.push_back(VkSl->GetVkLayout());
		}
	}

	std::vector<VkPushConstantRange> PcRanges;
	for (const auto& Pc : Desc.PushConstants)
	{
		VkPushConstantRange Range{};
		Range.offset = Pc.Offset;
		Range.size = Pc.Size;

		VkShaderStageFlags Flags = 0;
		if (RHIEnumHas(Pc.Stages, ERHIShaderStage::Vertex))
		{
			Flags |= VK_SHADER_STAGE_VERTEX_BIT;
		}
		if (RHIEnumHas(Pc.Stages, ERHIShaderStage::Fragment))
		{
			Flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
		}
		if (RHIEnumHas(Pc.Stages, ERHIShaderStage::Compute))
		{
			Flags |= VK_SHADER_STAGE_COMPUTE_BIT;
		}
		Range.stageFlags = Flags;

		PcRanges.push_back(Range);
	}

	VkPipelineLayoutCreateInfo Info{};
	Info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	Info.setLayoutCount = static_cast<std::uint32_t>(SetLayouts.size());
	Info.pSetLayouts = SetLayouts.data();
	Info.pushConstantRangeCount = static_cast<std::uint32_t>(PcRanges.size());
	Info.pPushConstantRanges = PcRanges.data();

	VkPipelineLayout Layout = VK_NULL_HANDLE;
	if (!CheckVkResult(vkCreatePipelineLayout(Device, &Info, nullptr, &Layout),
	                   "vkCreatePipelineLayout"))
	{
		return nullptr;
	}

	return new FVulkanPipelineLayout(Device, Layout);
}

void FVulkanRHI::DestroyPipelineLayout(FRHIPipelineLayout* Layout)
{
	delete Layout;
}

FRHIDescriptorPool* FVulkanRHI::CreateDescriptorPool(const FRHIDescriptorPoolDesc& Desc)
{
	std::vector<VkDescriptorPoolSize> PoolSizes;
	for (const auto& S : Desc.PoolSizes)
	{
		VkDescriptorPoolSize Size{};
		Size.type = ToVkDescriptorType(S.Type);
		Size.descriptorCount = S.Count;
		PoolSizes.push_back(Size);
	}

	VkDescriptorPoolCreateInfo Info{};
	Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	Info.maxSets = Desc.MaxSets;
	Info.poolSizeCount = static_cast<std::uint32_t>(PoolSizes.size());
	Info.pPoolSizes = PoolSizes.data();
	if (Desc.bUpdateAfterBind)
	{
		Info.flags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
	}

	VkDescriptorPool Pool = VK_NULL_HANDLE;
	if (!CheckVkResult(vkCreateDescriptorPool(Device, &Info, nullptr, &Pool),
	                   "vkCreateDescriptorPool"))
	{
		return nullptr;
	}

	return new FVulkanDescriptorPool(Device, Pool);
}

void FVulkanRHI::DestroyDescriptorPool(FRHIDescriptorPool* Pool)
{
	delete Pool;
}

FRHIDescriptorSet* FVulkanRHI::AllocateDescriptorSet(FRHIDescriptorPool* Pool, FRHIDescriptorSetLayout* Layout)
{
	auto* VkPool = static_cast<FVulkanDescriptorPool*>(Pool);
	auto* VkLayout = static_cast<FVulkanDescriptorSetLayout*>(Layout);

	VkDescriptorSetLayout VkSl = VkLayout->GetVkLayout();

	VkDescriptorSetAllocateInfo Info{};
	Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	Info.descriptorPool = VkPool->GetVkPool();
	Info.descriptorSetCount = 1;
	Info.pSetLayouts = &VkSl;

	VkDescriptorSet Set = VK_NULL_HANDLE;
	if (!CheckVkResult(vkAllocateDescriptorSets(Device, &Info, &Set),
	                   "vkAllocateDescriptorSets"))
	{
		return nullptr;
	}

	return new FVulkanDescriptorSet(Set);
}

void FVulkanRHI::FreeDescriptorSet(FRHIDescriptorPool* Pool, FRHIDescriptorSet* Set)
{
	auto* VkPool = static_cast<FVulkanDescriptorPool*>(Pool);
	auto* VkSet = static_cast<FVulkanDescriptorSet*>(Set);

	VkDescriptorSet VkDs = VkSet->GetVkSet();
	vkFreeDescriptorSets(Device, VkPool->GetVkPool(), 1, &VkDs);

	delete Set;
}

FRHIRenderPass* FVulkanRHI::CreateRenderPass(const FRHIRenderPassDesc& Desc)
{
	std::vector<VkAttachmentDescription> Attachments;
	std::vector<VkAttachmentReference> ColorRefs;

	for (const auto& ColorAtt : Desc.ColorAttachments)
	{
		VkAttachmentDescription Att{};
		Att.format = ToVkFormat(ColorAtt.Format);
		Att.samples = static_cast<VkSampleCountFlagBits>(ColorAtt.SampleCount);
		Att.loadOp = static_cast<VkAttachmentLoadOp>(ColorAtt.LoadOp);
		Att.storeOp = static_cast<VkAttachmentStoreOp>(ColorAtt.StoreOp);
		Att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		Att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		Att.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		Att.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference Ref{};
		Ref.attachment = static_cast<std::uint32_t>(Attachments.size());
		Ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		Attachments.push_back(Att);
		ColorRefs.push_back(Ref);
	}

	VkSubpassDescription Subpass{};
	Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	Subpass.colorAttachmentCount = static_cast<std::uint32_t>(ColorRefs.size());
	Subpass.pColorAttachments = ColorRefs.data();

	VkRenderPassCreateInfo Info{};
	Info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	Info.attachmentCount = static_cast<std::uint32_t>(Attachments.size());
	Info.pAttachments = Attachments.data();
	Info.subpassCount = 1;
	Info.pSubpasses = &Subpass;

	VkRenderPass Pass = VK_NULL_HANDLE;
	if (!CheckVkResult(vkCreateRenderPass(Device, &Info, nullptr, &Pass), "vkCreateRenderPass"))
	{
		return nullptr;
	}

	return new FVulkanRenderPass(Device, Pass);
}

void FVulkanRHI::DestroyRenderPass(FRHIRenderPass* Pass)
{
	delete Pass;
}

FRHIFramebuffer* FVulkanRHI::CreateFramebuffer(const FRHIFramebufferDesc& Desc)
{
	if (Desc.RenderPass == nullptr)
	{
		return nullptr;
	}

	auto* VkPas = static_cast<FVulkanRenderPass*>(Desc.RenderPass);
	std::vector<VkImageView> Views;
	for (auto* View : Desc.Attachments)
	{
		if (View != nullptr)
		{
			auto* VkView = static_cast<FVulkanTextureView*>(View);
			Views.push_back(VkView->GetVkImageView());
		}
	}

	VkFramebufferCreateInfo Info{};
	Info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	Info.renderPass = VkPas->GetVkPass();
	Info.attachmentCount = static_cast<std::uint32_t>(Views.size());
	Info.pAttachments = Views.data();
	Info.width = Desc.Width;
	Info.height = Desc.Height;
	Info.layers = 1;

	VkFramebuffer FB = VK_NULL_HANDLE;
	if (!CheckVkResult(vkCreateFramebuffer(Device, &Info, nullptr, &FB), "vkCreateFramebuffer"))
	{
		return nullptr;
	}

	return new FVulkanFramebuffer(Device, FB);
}

void FVulkanRHI::DestroyFramebuffer(FRHIFramebuffer* Framebuffer)
{
	delete Framebuffer;
}

bool FVulkanRHI::RecreateSwapchain()
{
	if (Device == VK_NULL_HANDLE)
	{
		return false;
	}

	vkDeviceWaitIdle(Device);

	DestroySwapchainResources();

	if (!CreateSwapchain())
	{
		return false;
	}

	if (!CreateImageViews())
	{
		return false;
	}

	if (!CreateFramebuffers())
	{
		return false;
	}

	MAHO_CORE_INFO("Vulkan swapchain recreated ({}x{})", SwapchainExtent.width, SwapchainExtent.height);
	return true;
}

void FRHIDeleter::operator()(IRHI* RHI) const
{
	delete RHI;
}

FRHIPtr FRHIFactory::Create(ERHIBackend Backend)
{
	switch (Backend)
	{
	case ERHIBackend::Vulkan:
	{
		auto* RHI = new FVulkanRHI();
		return FRHIPtr{RHI};
	}
	}

	MAHO_CORE_ERROR("FRHIFactory::Create: unsupported ERHIBackend ({})", static_cast<std::uint32_t>(Backend));
	return nullptr;
}

} // namespace Maho
