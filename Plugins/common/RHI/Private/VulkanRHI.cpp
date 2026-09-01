#include "VulkanRHI.h"

#include "VulkanResources.h"

#include <ConsoleVariable.h>
#include <Log.h>

#if defined(_WIN32)
#	include <windows.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>

namespace Maho
{

namespace
{

static ConsoleVariable::TAutoConsoleVariable<int> GCVarVSync(
	"r.VSync",
	1,
	"0=prefer Mailbox/Immediate, 1=FIFO (vsync)");

static ConsoleVariable::TAutoConsoleVariable<int> GCVarSwapchainExtraImages(
	"r.Swapchain.ExtraImages",
	1,
	"Extra swapchain images beyond minImageCount (clamped by device max)");

static ConsoleVariable::TAutoConsoleVariable<int> GCVarMinSwapchainImages(
	"r.MinSwapchainImages",
	2,
	"Reported minimum swapchain image count (ImGui / present contract)");

static ConsoleVariable::TAutoConsoleVariable<int> GCVarRHIValidation(
	"r.RHI.Validation",
	1,
	"0=off, 1=on (Khronos validation layers + debug messenger)");

/** Vulkan validation messenger callback -- routes every message to the Maho log. */
VKAPI_ATTR VkBool32 VKAPI_CALL GValidationCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT Severity,
	VkDebugUtilsMessageTypeFlagsEXT /*Type*/,
	const VkDebugUtilsMessengerCallbackDataEXT* CallbackData,
	void* /*UserData*/)
{
	if (CallbackData == nullptr || CallbackData->pMessage == nullptr)
	{
		return VK_FALSE;
	}
	// The logger may already be torn down: the engine shutdown graph runs the
	// IShutdown stages concurrently, so a message arriving during teardown can
	// race the Log layer's Shutdown (GetLog() == null). Fall back to stderr
	// instead of tripping the MAHO_ENSURE inside the log macros.
	if (FLog* Log = ::Maho::GetLog())
	{
		if (Severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
		{
			Log->Error("Vulkan validation: {}", CallbackData->pMessage);
		}
		else if (Severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		{
			Log->Warn("Vulkan validation: {}", CallbackData->pMessage);
		}
		else
		{
			Log->Info("Vulkan validation: {}", CallbackData->pMessage);
		}
	}
	else
	{
		std::fprintf(stderr, "[Vulkan validation] %s\n", CallbackData->pMessage);
	}
	return VK_FALSE;
}

/** Image view aspect from an ERHIFormat: depth(+stencil) formats are NOT color. */
VkImageAspectFlags GetImageAspectForFormat(ERHIFormat Format)
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

	MAHO_LOG_CORE_ERROR("Vulkan: {} failed with VkResult {}", Context, static_cast<int>(Result));
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
		MAHO_LOG_CORE_ERROR("FVulkanRHI::Initialize: already initialized");
		return false;
	}

	if (Desc.Backend != ERHIBackend::Vulkan)
	{
		MAHO_LOG_CORE_ERROR("FVulkanRHI::Initialize: unsupported backend ({})", static_cast<std::uint32_t>(Desc.Backend));
		return false;
	}

	if (Desc.NativeWindowHandle == nullptr)
	{
		MAHO_LOG_CORE_ERROR("FVulkanRHI::Initialize: NativeWindowHandle is null");
		return false;
	}

	if (Desc.FramebufferWidth <= 0 || Desc.FramebufferHeight <= 0)
	{
		MAHO_LOG_CORE_ERROR("FVulkanRHI::Initialize: invalid framebuffer size {}x{}", Desc.FramebufferWidth, Desc.FramebufferHeight);
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

	if (!CreateMemoryAllocator())
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
	MAHO_LOG_CORE_INFO("FVulkanRHI initialized ({}x{})", FramebufferWidth, FramebufferHeight);
	return true;
}

void FVulkanRHI::Shutdown()
{
	if (Device != VK_NULL_HANDLE)
	{
		vkDeviceWaitIdle(Device);
	}

	// Drop the debug messenger BEFORE any teardown: destroying objects while the
	// messenger is armed can emit validation messages, and those may arrive after
	// the engine's log layer has shut down (the shutdown graph runs the IShutdown
	// stages concurrently, so GetLog() is not guaranteed here). Nothing routes to
	// the callback after this point, so teardown noise can't reach a dead logger.
	if (DebugMessenger != VK_NULL_HANDLE)
	{
		auto DestroyFn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
			vkGetInstanceProcAddr(Instance, "vkDestroyDebugUtilsMessengerEXT"));
		if (DestroyFn != nullptr)
		{
			DestroyFn(Instance, DebugMessenger, nullptr);
		}
		DebugMessenger = VK_NULL_HANDLE;
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

	for (VkSemaphore Sem : RenderFinishedSemaphores)
	{
		if (Device != VK_NULL_HANDLE && Sem != VK_NULL_HANDLE)
		{
			vkDestroySemaphore(Device, Sem, nullptr);
		}
	}
	RenderFinishedSemaphores.clear();

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
		// Free the non-owning wrapper first (the Vk buffer dies with the pool).
		delete FrameCommandListRHI;
		FrameCommandListRHI = nullptr;
		vkDestroyCommandPool(Device, CommandPool, nullptr);
		CommandPool = VK_NULL_HANDLE;
		CommandBuffer = VK_NULL_HANDLE;
	}

	DestroySwapchainResources();

	// Free the non-owning render pass wrapper (skips Vk destroy; the Vk render
	// pass is destroyed below).
	if (SwapchainRenderPassRHI != nullptr)
	{
		delete SwapchainRenderPassRHI;
		SwapchainRenderPassRHI = nullptr;
	}

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

void FVulkanRHI::WaitIdle()
{
	if (Device != VK_NULL_HANDLE)
	{
		vkDeviceWaitIdle(Device);
	}
}

void FVulkanRHI::BeginFrame()
{
	if (!bInitialized)
	{
		MAHO_LOG_CORE_ERROR("FVulkanRHI::BeginFrame: not initialized");
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
		MAHO_LOG_CORE_INFO("FVulkanRHI::BeginFrame: swapchain suboptimal");
	}
	else if (!CheckVkResult(AcquireResult, "vkAcquireNextImageKHR"))
	{
		return;
	}

	// Begin the frame command buffer so features can record into it via the
	// borrowed command list. The wrapper keeps its recording flag in sync (the
	// buffer is reset/begun here, ended/submitted by EndFrame).
	if (FrameCommandListRHI == nullptr)
	{
		return;
	}
	FrameCommandListRHI->Begin();
}

FRHICommandList* FVulkanRHI::GetFrameCommandList()
{
	return FrameCommandListRHI;
}

VkImageView FVulkanRHI::GetRawTextureView(FRHITextureView* View) const
{
	// ImGui official-backend bridge: raw VkImageView of an RHI texture view
	// (ImGui renders into SceneColor). The wrapper's Vk handle is a member.
	return View != nullptr ? static_cast<const FVulkanTextureView*>(View)->GetVkImageView() : VK_NULL_HANDLE;
}

ERHIFormat FVulkanRHI::GetSwapchainFormat() const
{
	// The swapchain picks B8G8R8A8_SRGB when available; the off-screen scene
	// target must match exactly (blit requires identical formats).
	if (SwapchainImageFormat == VK_FORMAT_B8G8R8A8_SRGB)
	{
		return ERHIFormat::B8G8R8A8_SRGB;
	}
	if (SwapchainImageFormat == VK_FORMAT_B8G8R8A8_UNORM)
	{
		return ERHIFormat::B8G8R8A8_UNORM;
	}
	if (SwapchainImageFormat == VK_FORMAT_R8G8B8A8_SRGB)
	{
		return ERHIFormat::R8G8B8A8_UNORM;
	}
	if (SwapchainImageFormat == VK_FORMAT_R8G8B8A8_UNORM)
	{
		return ERHIFormat::R8G8B8A8_UNORM;
	}
	return ERHIFormat::B8G8R8A8_UNORM;
}

void FVulkanRHI::PresentTexture(FRHITexture* Src)
{
	if (!bInitialized)
	{
		MAHO_LOG_CORE_ERROR("FVulkanRHI::PresentTexture: not initialized");
		return;
	}

	auto* SrcVk = static_cast<FVulkanTexture*>(Src);
	if (SrcVk == nullptr || SrcVk->GetVkImage() == VK_NULL_HANDLE)
	{
		MAHO_LOG_CORE_ERROR("FVulkanRHI::PresentTexture: null source texture");
		return;
	}

	const VkImage SrcImage = SrcVk->GetVkImage();
	const VkImage DstImage = SwapchainImages[CurrentImageIndex];
	const FRHITextureDesc& SrcDesc = SrcVk->GetDesc();

	// Transition source to transfer-src and the swapchain image to transfer-dst.
	auto TransitionImage = [this](VkImage Image, VkImageLayout Old, VkImageLayout New,
		VkAccessFlags SrcAccess, VkAccessFlags DstAccess, VkPipelineStageFlags SrcStage,
		VkPipelineStageFlags DstStage, std::uint32_t MipLevels, std::uint32_t Layers)
	{
		VkImageMemoryBarrier Barrier{};
		Barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		Barrier.oldLayout = Old;
		Barrier.newLayout = New;
		Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		Barrier.image = Image;
		Barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		Barrier.subresourceRange.baseMipLevel = 0;
		Barrier.subresourceRange.levelCount = MipLevels;
		Barrier.subresourceRange.baseArrayLayer = 0;
		Barrier.subresourceRange.layerCount = Layers;
		Barrier.srcAccessMask = SrcAccess;
		Barrier.dstAccessMask = DstAccess;
		vkCmdPipelineBarrier(CommandBuffer, SrcStage, DstStage, 0, 0, nullptr, 0, nullptr, 1, &Barrier);
	};

	TransitionImage(SrcImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		SrcDesc.MipLevels, SrcDesc.ArrayLayers);

	TransitionImage(DstImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		0, VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		1, 1);

	VkImageBlit Blit{};
	Blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	Blit.srcSubresource.mipLevel = 0;
	Blit.srcSubresource.baseArrayLayer = 0;
	Blit.srcSubresource.layerCount = 1;
	Blit.srcOffsets[0] = { 0, 0, 0 };
	Blit.srcOffsets[1] = {
		static_cast<std::int32_t>(SrcDesc.Extent.Width),
		static_cast<std::int32_t>(SrcDesc.Extent.Height),
		1 };
	Blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	Blit.dstSubresource.mipLevel = 0;
	Blit.dstSubresource.baseArrayLayer = 0;
	Blit.dstSubresource.layerCount = 1;
	Blit.dstOffsets[0] = { 0, 0, 0 };
	Blit.dstOffsets[1] = {
		static_cast<std::int32_t>(SwapchainExtent.width),
		static_cast<std::int32_t>(SwapchainExtent.height),
		1 };

	vkCmdBlitImage(
		CommandBuffer,
		SrcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		DstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &Blit, VK_FILTER_LINEAR);

	// Transition the swapchain image to present-src before submit.
	TransitionImage(DstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VK_ACCESS_TRANSFER_WRITE_BIT, 0,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		1, 1);

	// Return the source to color-attachment so the next frame can render into it.
	TransitionImage(SrcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		SrcDesc.MipLevels, SrcDesc.ArrayLayers);
}

void FVulkanRHI::EndFrame()
{
	if (!bInitialized)
	{
		MAHO_LOG_CORE_ERROR("FVulkanRHI::EndFrame: not initialized");
		return;
	}

	// Close the frame command buffer (opened by BeginFrame, recorded by features
	// + PresentTexture) before submitting.
	if (FrameCommandListRHI != nullptr)
	{
		FrameCommandListRHI->End();
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
	SubmitInfo.pSignalSemaphores = RenderFinishedSemaphores.empty()
		? nullptr
		: &RenderFinishedSemaphores[CurrentImageIndex < RenderFinishedSemaphores.size() ? CurrentImageIndex : 0];

	if (!CheckVkResult(vkQueueSubmit(GraphicsVkQueue, 1, &SubmitInfo, InFlightFence), "vkQueueSubmit"))
	{
		return;
	}

	VkPresentInfoKHR PresentInfo{};
	PresentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	PresentInfo.waitSemaphoreCount = 1;
	PresentInfo.pWaitSemaphores = RenderFinishedSemaphores.empty()
		? nullptr
		: &RenderFinishedSemaphores[CurrentImageIndex < RenderFinishedSemaphores.size() ? CurrentImageIndex : 0];
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
		MAHO_LOG_CORE_ERROR("FVulkanRHI::Resize: invalid size {}x{}", Width, Height);
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
	// 1.2+ required for drawIndirectCount / bufferDeviceAddress / descriptorIndexing;
	// 1.3 for dynamic rendering (vkCmdBeginRendering) -- requesting 1.3 also makes
	// the loader/validation layer dispatch the 1.3 global functions correctly.
	AppInfo.apiVersion = VK_API_VERSION_1_3;

	std::vector<const char*> InstanceExtensions(GInstanceExtensions, GInstanceExtensions + std::size(GInstanceExtensions));
	std::vector<const char*> InstanceLayers;

	const bool bValidation = GCVarRHIValidation.GetValue() != 0;
	if (bValidation)
	{
		// Only enable the Khronos validation layer if it is actually installed.
		std::uint32_t LayerCount = 0;
		vkEnumerateInstanceLayerProperties(&LayerCount, nullptr);
		std::vector<VkLayerProperties> Layers(LayerCount);
		vkEnumerateInstanceLayerProperties(&LayerCount, Layers.data());
		bool bFound = false;
		for (const auto& Layer : Layers)
		{
			if (std::strcmp(Layer.layerName, "VK_LAYER_KHRONOS_validation") == 0)
			{
				bFound = true;
				break;
			}
		}
		if (bFound)
		{
			InstanceLayers.push_back("VK_LAYER_KHRONOS_validation");
			InstanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
			MAHO_LOG_CORE_INFO("Vulkan validation layers enabled");
		}
		else
		{
			MAHO_LOG_CORE_WARN(
				"Vulkan validation requested (r.RHI.Validation=1) but VK_LAYER_KHRONOS_validation is not installed");
		}
	}

	VkInstanceCreateInfo CreateInfo{};
	CreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	CreateInfo.pApplicationInfo = &AppInfo;
	CreateInfo.enabledExtensionCount = static_cast<std::uint32_t>(InstanceExtensions.size());
	CreateInfo.ppEnabledExtensionNames = InstanceExtensions.data();
	CreateInfo.enabledLayerCount = static_cast<std::uint32_t>(InstanceLayers.size());
	CreateInfo.ppEnabledLayerNames = InstanceLayers.data();

	if (!CheckVkResult(vkCreateInstance(&CreateInfo, nullptr, &Instance), "vkCreateInstance"))
	{
		return false;
	}

	if (bValidation)
	{
		CreateDebugMessenger();
	}

	MAHO_LOG_CORE_INFO("Vulkan instance created");
	return true;
}

void FVulkanRHI::CreateDebugMessenger()
{
	auto CreateFn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
		vkGetInstanceProcAddr(Instance, "vkCreateDebugUtilsMessengerEXT"));
	if (CreateFn == nullptr)
	{
		MAHO_LOG_CORE_WARN("Vulkan validation: vkCreateDebugUtilsMessengerEXT unavailable");
		return;
	}

	VkDebugUtilsMessengerCreateInfoEXT Info{};
	Info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	Info.messageSeverity =
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	Info.messageType =
		VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	Info.pfnUserCallback = GValidationCallback;

	if (CreateFn(Instance, &Info, nullptr, &DebugMessenger) != VK_SUCCESS)
	{
		MAHO_LOG_CORE_WARN("Vulkan validation: failed to create debug messenger");
		DebugMessenger = VK_NULL_HANDLE;
	}
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

	MAHO_LOG_CORE_INFO("Vulkan Win32 surface created");
	return true;
#else
	MAHO_LOG_CORE_ERROR("FVulkanRHI::CreateSurface: unsupported platform");
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
		MAHO_LOG_CORE_ERROR("FVulkanRHI::PickPhysicalDevice: no Vulkan-capable GPU found");
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
			MAHO_LOG_CORE_INFO("Vulkan physical device selected: {} (discrete)", Properties.deviceName);
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
				MAHO_LOG_CORE_INFO("Vulkan physical device selected: {} (fallback)", Properties.deviceName);
				break;
			}
		}
	}

	if (SelectedDevice == VK_NULL_HANDLE)
	{
		MAHO_LOG_CORE_ERROR("FVulkanRHI::PickPhysicalDevice: no suitable GPU found");
		return false;
	}

	PhysicalDevice = SelectedDevice;

	if (!FindQueueFamilies(PhysicalDevice))
	{
		MAHO_LOG_CORE_ERROR("FVulkanRHI::PickPhysicalDevice: queue families not found");
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
	// GPU-driven pipeline core features (Vulkan 1.1/1.2 core).
	DeviceFeatures.multiDrawIndirect = VK_TRUE;
	DeviceFeatures.drawIndirectFirstInstance = VK_TRUE;
	DeviceFeatures.fragmentStoresAndAtomics = VK_TRUE;
	DeviceFeatures.vertexPipelineStoresAndAtomics = VK_TRUE;
	DeviceFeatures.shaderStorageBufferArrayDynamicIndexing = VK_TRUE;
	DeviceFeatures.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
	DeviceFeatures.fillModeNonSolid = VK_TRUE;

	VkPhysicalDeviceDynamicRenderingFeatures DynamicRendering{};
	DynamicRendering.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
	DynamicRendering.dynamicRendering = VK_TRUE;

	// Vulkan 1.2 core: GPU-controlled indirect draw count.
	VkPhysicalDeviceVulkan11Features Features11{};
	Features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
	Features11.shaderDrawParameters = VK_TRUE;

	// The descriptor-indexing features live in the 1.2 struct (they were promoted
	// from VkPhysicalDeviceDescriptorIndexingFeatures -- do NOT also append that
	// struct, validation rejects two overlapping feature structures).
	VkPhysicalDeviceVulkan12Features Features12{};
	Features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	Features12.drawIndirectCount = VK_TRUE;
	Features12.bufferDeviceAddress = VK_TRUE;
	Features12.descriptorIndexing = VK_TRUE;
	Features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
	Features12.descriptorBindingPartiallyBound = VK_TRUE;
	Features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
	Features12.runtimeDescriptorArray = VK_TRUE;
	Features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
	Features12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
	Features12.shaderUniformBufferArrayNonUniformIndexing = VK_TRUE;
	Features12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;

	// Ray tracing is optional; enable it only when the KHR extensions were
	// requested. Skipped for now (device creation must not fail on optional RT).

	Features11.pNext = &Features12;
	DynamicRendering.pNext = &Features11;

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

			// Ray tracing KHR device functions - resolve dynamically (not in the
	// static loader on all platforms). Presence == capability (extensions were
	// requested above; missing symbols mean the driver refuses RT).
	CreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
		vkGetDeviceProcAddr(Device, "vkCreateAccelerationStructureKHR"));
	DestroyAccelerationStructureKHR = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
		vkGetDeviceProcAddr(Device, "vkDestroyAccelerationStructureKHR"));
	GetAccelerationStructureBuildSizesKHR = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
		vkGetDeviceProcAddr(Device, "vkGetAccelerationStructureBuildSizesKHR"));
	CreateRayTracingPipelinesKHR = reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(
		vkGetDeviceProcAddr(Device, "vkCreateRayTracingPipelinesKHR"));
	GetRayTracingShaderGroupHandlesKHR = reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(
		vkGetDeviceProcAddr(Device, "vkGetRayTracingShaderGroupHandlesKHR"));
	CmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
		vkGetDeviceProcAddr(Device, "vkCmdBuildAccelerationStructuresKHR"));
	CmdCopyAccelerationStructureKHR = reinterpret_cast<PFN_vkCmdCopyAccelerationStructureKHR>(
		vkGetDeviceProcAddr(Device, "vkCmdCopyAccelerationStructureKHR"));
	CmdTraceRaysKHR = reinterpret_cast<PFN_vkCmdTraceRaysKHR>(
		vkGetDeviceProcAddr(Device, "vkCmdTraceRaysKHR"));
	GetBufferDeviceAddressKHR = reinterpret_cast<PFN_vkGetBufferDeviceAddressKHR>(
		vkGetDeviceProcAddr(Device, "vkGetBufferDeviceAddressKHR"));

	bRayTracingSupported =
		CreateAccelerationStructureKHR != nullptr
		&& DestroyAccelerationStructureKHR != nullptr
		&& GetAccelerationStructureBuildSizesKHR != nullptr
		&& CreateRayTracingPipelinesKHR != nullptr
		&& GetRayTracingShaderGroupHandlesKHR != nullptr
		&& CmdBuildAccelerationStructuresKHR != nullptr
		&& CmdCopyAccelerationStructureKHR != nullptr
		&& CmdTraceRaysKHR != nullptr;
	if (bRayTracingSupported)
	{
		MAHO_LOG_CORE_INFO("Vulkan ray tracing (KHR) available");
	}
	else
	{
		MAHO_LOG_CORE_WARN("Vulkan ray tracing (KHR) NOT available - RT APIs will fail");
	}

	MAHO_LOG_CORE_INFO(
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
		MAHO_LOG_CORE_INFO("Transfer: fallback to Graphics (no dedicated TRANSFER family)");
	}
	else
	{
		MAHO_LOG_CORE_INFO("Transfer: dedicated");
	}

	return true;
}

void FVulkanRHI::DestroySwapchainResources()
{
	// Free the non-owning RHI wrappers first (they skip Vk destroy; the swapchain
	// frees the Vk handles below).
	for (FRHIFramebuffer* FB : SwapchainFramebufferRHI)
	{
		delete FB;
	}
	SwapchainFramebufferRHI.clear();

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

	// A hidden / not-yet-mapped window reports a 0 extent; fall back to the
	// requested framebuffer size so vkCreateSwapchainKHR does not fail with
	// VK_ERROR_INITIALIZATION_FAILED.
	if (SwapchainExtent.width == 0 || SwapchainExtent.height == 0)
	{
		SwapchainExtent.width = static_cast<std::uint32_t>(FramebufferWidth);
		SwapchainExtent.height = static_cast<std::uint32_t>(FramebufferHeight);
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
	CreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
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
		Swapchain = VK_NULL_HANDLE;
		MAHO_LOG_CORE_ERROR(
			"vkCreateSwapchainKHR inputs: format={} colorspace={} extent={}x{} images={} presentMode={} curExtent={}x{} min={}x{} max={}x{}",
			static_cast<int>(CreateInfo.imageFormat),
			static_cast<int>(CreateInfo.imageColorSpace),
			CreateInfo.imageExtent.width, CreateInfo.imageExtent.height,
			CreateInfo.minImageCount,
			static_cast<int>(CreateInfo.presentMode),
			Capabilities.currentExtent.width, Capabilities.currentExtent.height,
			Capabilities.minImageExtent.width, Capabilities.minImageExtent.height,
			Capabilities.maxImageExtent.width, Capabilities.maxImageExtent.height);
		return false;
	}

	std::uint32_t SwapchainImageCount = 0;
	vkGetSwapchainImagesKHR(Device, Swapchain, &SwapchainImageCount, nullptr);
	SwapchainImages.resize(SwapchainImageCount);
	vkGetSwapchainImagesKHR(Device, Swapchain, &SwapchainImageCount, SwapchainImages.data());

	MAHO_LOG_CORE_INFO(
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

	MAHO_LOG_CORE_INFO("Vulkan swapchain image views created ({})", SwapchainImageViews.size());
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

	MAHO_LOG_CORE_INFO("Vulkan render pass created");
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

	MAHO_LOG_CORE_INFO("Vulkan framebuffers created ({})", SwapchainFramebuffers.size());

	// Non-owning RHI views of the swapchain framebuffers (the swapchain owns the
	// Vk handles; these wrappers skip destroy and are only for BeginRenderPass).
	SwapchainFramebufferRHI.resize(SwapchainFramebuffers.size());
	for (std::size_t Index = 0; Index < SwapchainFramebuffers.size(); ++Index)
	{
		SwapchainFramebufferRHI[Index] = new FVulkanFramebuffer(Device, SwapchainFramebuffers[Index], /*bOwnsHandle=*/false);
	}
	if (SwapchainRenderPassRHI == nullptr)
	{
		SwapchainRenderPassRHI = new FVulkanRenderPass(Device, RenderPass, /*bOwnsHandle=*/false);
	}
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

	// Non-owning wrapper so features borrow the frame buffer via GetFrameCommandList.
	FVulkanCommandList::FRTRuntime RT;
	RT.BuildAccel = CmdBuildAccelerationStructuresKHR;
	RT.CopyAccel = CmdCopyAccelerationStructureKHR;
	RT.TraceRays = CmdTraceRaysKHR;
	FrameCommandListRHI = new FVulkanCommandList(
		ERHICommandListType::Graphics, Device, CommandPool, CommandBuffer, MemoryAllocator.get(), RT);

	MAHO_LOG_CORE_INFO("Vulkan command pool and buffer created");
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

	if (!CreateRenderFinishedSemaphores())
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

	MAHO_LOG_CORE_INFO("Vulkan sync objects created");
	return true;
}

bool FVulkanRHI::CreateRenderFinishedSemaphores()
{
	// One render-finished semaphore per swapchain image, indexed by
	// CurrentImageIndex. A present on image i waits semaphore[i]; reusing a single
	// semaphore while an older present is still pending is a Vulkan error
	// (VUID-vkQueueSubmit-pSignalSemaphores-00067).
	for (VkSemaphore Sem : RenderFinishedSemaphores)
	{
		if (Device != VK_NULL_HANDLE && Sem != VK_NULL_HANDLE)
		{
			vkDestroySemaphore(Device, Sem, nullptr);
		}
	}
	RenderFinishedSemaphores.clear();

	VkSemaphoreCreateInfo SemaphoreInfo{};
	SemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	RenderFinishedSemaphores.reserve(SwapchainImages.size());
	for (std::size_t I = 0; I < SwapchainImages.size(); ++I)
	{
		VkSemaphore Sem = VK_NULL_HANDLE;
		if (!CheckVkResult(vkCreateSemaphore(Device, &SemaphoreInfo, nullptr, &Sem), "vkCreateSemaphore (render finished)"))
		{
			return false;
		}
		RenderFinishedSemaphores.push_back(Sem);
	}
	return true;
}

bool FVulkanRHI::CreateMemoryAllocator()
{
	MemoryAllocator = std::make_unique<FVulkanMemoryAllocator>();
	if (!MemoryAllocator->Initialize(Instance, PhysicalDevice, Device))
	{
		return false;
	}

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

IDynamicRHIMemoryAllocator* FVulkanRHI::GetMemoryAllocator()
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
	// Each command list gets its OWN command pool: Vulkan command pools are NOT
	// thread-safe, and the RDG records feature lists concurrently on the recording
	// pool -- sharing a pool across threads corrupts allocation/recording.
	std::uint32_t Family = GraphicsQueueFamilyIndex;
	if (Type == ERHICommandListType::Compute)
	{
		Family = ComputeQueueFamilyIndex;
	}
	else if (Type == ERHICommandListType::Transfer)
	{
		Family = TransferQueueFamilyIndex;
	}

	VkCommandPoolCreateInfo PoolInfo{};
	PoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	PoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;   // per-list vkResetCommandBuffer
	PoolInfo.queueFamilyIndex = Family;

	VkCommandPool Pool = VK_NULL_HANDLE;
	if (!CheckVkResult(vkCreateCommandPool(Device, &PoolInfo, nullptr, &Pool), "vkCreateCommandPool"))
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
		vkDestroyCommandPool(Device, Pool, nullptr);
		return nullptr;
	}

	FVulkanCommandList::FRTRuntime RT;
	RT.BuildAccel = CmdBuildAccelerationStructuresKHR;
	RT.CopyAccel = CmdCopyAccelerationStructureKHR;
	RT.TraceRays = CmdTraceRaysKHR;
	return new FVulkanCommandList(Type, Device, Pool, CmdBuffer, MemoryAllocator.get(), RT);
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
	if (Device != VK_NULL_HANDLE && Pool != VK_NULL_HANDLE)
	{
		vkDestroyCommandPool(Device, Pool, nullptr);
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

void FVulkanRHI::ResetFence(FRHIFence* Fence)
{
	if (Fence == nullptr)
	{
		return;
	}
	VkFence Handle = static_cast<FVulkanFence*>(Fence)->GetVkFence();
	vkResetFences(Device, 1, &Handle);
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
	if (RHIEnumHas(Usage, ERHIBufferUsage::DeviceAddress))
	{
		Flags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	}
	if (RHIEnumHas(Usage, ERHIBufferUsage::AccelerationStructure))
	{
		Flags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
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
	case ERHIFormat::B8G8R8A8_SRGB:
		return VK_FORMAT_B8G8R8A8_SRGB;
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
	// ERHIDescriptorType is dense and skips Vulkan texel-buffer enums - never static_cast.
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

	// Query the GPU device address when the buffer is shader-addressable.
	std::uint64_t DeviceAddress = 0;
	if (RHIEnumHas(Desc.Usage, ERHIBufferUsage::DeviceAddress))
	{
		VkBufferDeviceAddressInfo AddressInfo{};
		AddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
		AddressInfo.buffer = Buffer;
		DeviceAddress = vkGetBufferDeviceAddress(Device, &AddressInfo);
	}

	return new FVulkanBuffer(Desc, Buffer, Allocation, MemoryAllocator.get(), DeviceAddress);
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
		MAHO_LOG_CORE_ERROR("FVulkanRHI::CreateGraphicsPipeline: missing shader modules");
		return nullptr;
	}

	auto* VkVs = static_cast<FVulkanShaderModule*>(Desc.VertexShader);
	auto* VkFs = static_cast<FVulkanShaderModule*>(Desc.FragmentShader);

	if (VkVs->GetVkShaderModule() == VK_NULL_HANDLE || VkFs->GetVkShaderModule() == VK_NULL_HANDLE)
	{
		MAHO_LOG_CORE_ERROR("FVulkanRHI::CreateGraphicsPipeline: invalid shader modules");
		return nullptr;
	}

	VkRenderPass VkRp = VK_NULL_HANDLE;
	VkPipelineRenderingCreateInfo RenderingInfo{};
	// ColorFmt must outlive the else block -- RenderingInfo.pColorAttachmentFormats
	// is a POINTER to it, read later by vkCreateGraphicsPipelines. Declaring it
	// inside the block made the pointer dangle (garbage format -> broken pipeline).
	VkFormat ColorFmt = VK_FORMAT_UNDEFINED;
	if (Desc.RenderPass != nullptr)
	{
		auto* VkPass = static_cast<FVulkanRenderPass*>(Desc.RenderPass);
		VkRp = VkPass->GetVkPass();
	}
	else
	{
					// Dynamic rendering - declare attachment formats via pNext
		RenderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		RenderingInfo.colorAttachmentCount = 1;
		ColorFmt = ToVkFormat(Desc.ColorFormat);
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

	MAHO_LOG_CORE_INFO("FVulkanRHI: created graphics pipeline");
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
		MAHO_LOG_CORE_ERROR("FVulkanRHI::CreateComputePipeline: missing compute shader");
		return nullptr;
	}

	auto* VkCs = static_cast<FVulkanShaderModule*>(Desc.ComputeShader);
	if (VkCs->GetVkShaderModule() == VK_NULL_HANDLE)
	{
		MAHO_LOG_CORE_ERROR("FVulkanRHI::CreateComputePipeline: invalid compute shader");
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

	MAHO_LOG_CORE_INFO("FVulkanRHI: created compute pipeline");
	return new FVulkanComputePipeline(Device, Pipeline, VkPLayout);
}

void FVulkanRHI::DestroyComputePipeline(FRHIComputePipeline* Pipeline)
{
	delete Pipeline;
}

namespace
{
	[[nodiscard]] VkShaderStageFlagBits ToVkShaderStage(ERHIShaderStage Stage)
	{
		switch (Stage)
		{
		case ERHIShaderStage::RayGen:
			return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
		case ERHIShaderStage::AnyHit:
			return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
		case ERHIShaderStage::ClosestHit:
			return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
		case ERHIShaderStage::Miss:
			return VK_SHADER_STAGE_MISS_BIT_KHR;
		case ERHIShaderStage::Intersection:
			return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
		case ERHIShaderStage::Callable:
			return VK_SHADER_STAGE_CALLABLE_BIT_KHR;
		default:
			return VK_SHADER_STAGE_ALL;
		}
	}
}

FRHIRayTracingPipeline* FVulkanRHI::CreateRayTracingPipeline(const FRHIRayTracingPipelineDesc& Desc)
{
	if (!bRayTracingSupported || CreateRayTracingPipelinesKHR == nullptr)
	{
		MAHO_LOG_CORE_ERROR("FVulkanRHI::CreateRayTracingPipeline: ray tracing unavailable");
		return nullptr;
	}
	if (Desc.RayGen == nullptr || Desc.Layout == nullptr)
	{
		MAHO_LOG_CORE_ERROR("FVulkanRHI::CreateRayTracingPipeline: missing ray-gen shader or layout");
		return nullptr;
	}

	// -- stages: every module with its entry point --
	std::vector<VkPipelineShaderStageCreateInfo> Stages;
	std::vector<VkRayTracingShaderGroupCreateInfoKHR> Groups;
	Stages.reserve(16);
	Groups.reserve(16);

	auto AddModule = [&](FRHIShaderModule* Module, ERHIShaderStage Stage)
	{
		if (Module == nullptr)
		{
			return;
		}
		auto* VkModule = static_cast<FVulkanShaderModule*>(Module);
		VkPipelineShaderStageCreateInfo StageInfo{};
		StageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		StageInfo.stage = ToVkShaderStage(Stage);
		StageInfo.module = VkModule->GetVkShaderModule();
		StageInfo.pName = "main";
		Stages.push_back(StageInfo);
	};

	const std::uint32_t RayGenIndex = static_cast<std::uint32_t>(Stages.size());
	AddModule(Desc.RayGen, ERHIShaderStage::RayGen);

	for (FRHIShaderModule* Module : Desc.Miss)
	{
		AddModule(Module, ERHIShaderStage::Miss);
	}
	const std::uint32_t MissCount = static_cast<std::uint32_t>(Desc.Miss.size());
	const std::uint32_t MissFirstIndex = RayGenIndex + 1;

	for (FRHIShaderModule* Module : Desc.ClosestHit)
	{
		AddModule(Module, ERHIShaderStage::ClosestHit);
	}
	const std::uint32_t ClosestHitCount = static_cast<std::uint32_t>(Desc.ClosestHit.size());
	const std::uint32_t ClosestHitFirstIndex = MissFirstIndex + MissCount;

	for (FRHIShaderModule* Module : Desc.AnyHit)
	{
		AddModule(Module, ERHIShaderStage::AnyHit);
	}
	for (FRHIShaderModule* Module : Desc.Intersection)
	{
		AddModule(Module, ERHIShaderStage::Intersection);
	}
	for (FRHIShaderModule* Module : Desc.Callable)
	{
		AddModule(Module, ERHIShaderStage::Callable);
	}

	// -- groups: raygen / miss x N / hit (closest+any+intersection per geometry) --
	{
		VkRayTracingShaderGroupCreateInfoKHR Group{};
		Group.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		Group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		Group.generalShader = RayGenIndex;
		Group.closestHitShader = VK_SHADER_UNUSED_KHR;
		Group.anyHitShader = VK_SHADER_UNUSED_KHR;
		Group.intersectionShader = VK_SHADER_UNUSED_KHR;
		Groups.push_back(Group);
	}

	for (std::uint32_t I = 0; I < MissCount; ++I)
	{
		VkRayTracingShaderGroupCreateInfoKHR Group{};
		Group.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		Group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		Group.generalShader = MissFirstIndex + I;
		Group.closestHitShader = VK_SHADER_UNUSED_KHR;
		Group.anyHitShader = VK_SHADER_UNUSED_KHR;
		Group.intersectionShader = VK_SHADER_UNUSED_KHR;
		Groups.push_back(Group);
	}

	const std::uint32_t HitGroupCount = ClosestHitCount > 0 ? ClosestHitCount : 1;
	for (std::uint32_t I = 0; I < HitGroupCount; ++I)
	{
		VkRayTracingShaderGroupCreateInfoKHR Group{};
		Group.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		Group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
		Group.generalShader = VK_SHADER_UNUSED_KHR;
		Group.closestHitShader =
			I < ClosestHitCount ? ClosestHitFirstIndex + I : VK_SHADER_UNUSED_KHR;
		// any-hit and intersection are index-matched to the same geometry index.
		Group.anyHitShader = I < Desc.AnyHit.size()
			? MissFirstIndex + MissCount + ClosestHitCount + I
			: VK_SHADER_UNUSED_KHR;
		Group.intersectionShader = VK_SHADER_UNUSED_KHR;
		Groups.push_back(Group);
	}

	auto* VkLayout = static_cast<FVulkanPipelineLayout*>(Desc.Layout);

	VkRayTracingPipelineCreateInfoKHR PipelineInfo{};
	PipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
	PipelineInfo.stageCount = static_cast<std::uint32_t>(Stages.size());
	PipelineInfo.pStages = Stages.data();
	PipelineInfo.groupCount = static_cast<std::uint32_t>(Groups.size());
	PipelineInfo.pGroups = Groups.data();
	PipelineInfo.maxPipelineRayRecursionDepth = Desc.MaxRecursionDepth > 0 ? Desc.MaxRecursionDepth : 1;
	PipelineInfo.layout = VkLayout->GetVkLayout();

	VkPipeline Pipeline = VK_NULL_HANDLE;
	if (!CheckVkResult(
			CreateRayTracingPipelinesKHR(Device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &PipelineInfo, nullptr, &Pipeline),
			"vkCreateRayTracingPipelinesKHR"))
	{
		return nullptr;
	}

	MAHO_LOG_CORE_INFO("FVulkanRHI: created ray tracing pipeline ({} stages, {} groups)",
		Stages.size(), Groups.size());
	return new FVulkanRayTracingPipeline(Device, Pipeline, VkLayout->GetVkLayout());
}

void FVulkanRHI::DestroyRayTracingPipeline(FRHIRayTracingPipeline* Pipeline)
{
	delete Pipeline;
}

FRHIAccelerationStructure* FVulkanRHI::CreateAccelerationStructure(const FRHIRayTracingGeometryDesc& Desc)
{
	if (!bRayTracingSupported || CreateAccelerationStructureKHR == nullptr || GetAccelerationStructureBuildSizesKHR == nullptr)
	{
		MAHO_LOG_CORE_ERROR("FVulkanRHI::CreateAccelerationStructure: ray tracing unavailable");
		return nullptr;
	}
	if (Desc.Geometries.empty())
	{
		MAHO_LOG_CORE_ERROR("FVulkanRHI::CreateAccelerationStructure: no geometries");
		return nullptr;
	}

	// -- build sizes (accel + scratch) from the geometry list --
	std::vector<VkAccelerationStructureGeometryKHR> Geometries;
	Geometries.reserve(Desc.Geometries.size());

	for (const FRHIRayTracingGeometry& Geometry : Desc.Geometries)
	{
		VkAccelerationStructureGeometryKHR Geo{};
		Geo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
		Geo.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
		Geo.flags = Geometry.bOpaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0;
		VkAccelerationStructureGeometryTrianglesDataKHR& Triangles = Geo.geometry.triangles;
		Triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
		Triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		Triangles.vertexStride = Geometry.VertexStride;
		Triangles.maxVertex = Geometry.VertexCount > 0 ? Geometry.VertexCount - 1 : 0;
		if (Geometry.IndexBuffer)
		{
			Triangles.indexType = Geometry.bIndex32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
		}
		Geometries.push_back(Geo);
	}

	VkAccelerationStructureBuildGeometryInfoKHR BuildInfo{};
	BuildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	BuildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	BuildInfo.flags = Desc.bAllowUpdate
		? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR
		: 0u;
	BuildInfo.geometryCount = static_cast<std::uint32_t>(Geometries.size());
	BuildInfo.pGeometries = Geometries.data();

	std::vector<std::uint32_t> PrimitiveCounts;
	PrimitiveCounts.reserve(Desc.Geometries.size());
	for (const FRHIRayTracingGeometry& Geometry : Desc.Geometries)
	{
		PrimitiveCounts.push_back(
			Geometry.IndexCount > 0 ? Geometry.IndexCount / 3 : Geometry.VertexCount / 3);
	}

	VkAccelerationStructureBuildSizesInfoKHR Sizes{};
	Sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	GetAccelerationStructureBuildSizesKHR(
		Device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&BuildInfo,
		PrimitiveCounts.data(),
		&Sizes);

	if (Sizes.accelerationStructureSize == 0)
	{
		MAHO_LOG_CORE_ERROR("FVulkanRHI::CreateAccelerationStructure: zero accel size");
		return nullptr;
	}

	// -- storage buffer (GPU-only, acceleration-structure flag + device address) --
	VkBufferCreateInfo BufferInfo{};
	BufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	BufferInfo.size = Sizes.accelerationStructureSize;
	BufferInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
		| VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	BufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo AllocInfo = FVulkanMemoryAllocator::MakeAllocationInfo(ERHIMemoryUsage::GPUOnly);
	VkBuffer StorageBuffer = VK_NULL_HANDLE;
	VmaAllocation StorageAllocation = nullptr;
	if (!MemoryAllocator->CreateBuffer(BufferInfo, AllocInfo, StorageBuffer, StorageAllocation))
	{
		return nullptr;
	}

	VkBufferDeviceAddressInfo AddressInfo{};
	AddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	AddressInfo.buffer = StorageBuffer;
	const std::uint64_t BufferAddress = vkGetBufferDeviceAddress(Device, &AddressInfo);

	// -- create the acceleration structure object --
	VkAccelerationStructureCreateInfoKHR AccelInfo{};
	AccelInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	AccelInfo.buffer = StorageBuffer;
	AccelInfo.offset = 0;
	AccelInfo.size = Sizes.accelerationStructureSize;
	AccelInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

	VkAccelerationStructureKHR Accel = VK_NULL_HANDLE;
	if (!CheckVkResult(
			CreateAccelerationStructureKHR(Device, &AccelInfo, nullptr, &Accel),
			"vkCreateAccelerationStructureKHR"))
	{
		MemoryAllocator->DestroyBuffer(StorageBuffer, StorageAllocation);
		return nullptr;
	}

	MAHO_LOG_CORE_INFO("FVulkanRHI: created BLAS ({} geometries, {} bytes)",
		Desc.Geometries.size(), Sizes.accelerationStructureSize);
	return new FVulkanAccelerationStructure(
		Device, Accel, StorageBuffer, StorageAllocation, MemoryAllocator.get(),
		BufferAddress, Desc);
}

void FVulkanRHI::DestroyAccelerationStructure(FRHIAccelerationStructure* Accel)
{
	delete Accel;
}

bool FVulkanRHI::GetAccelerationStructureBuildSizes(
	const FRHIRayTracingGeometryDesc& Desc,
	std::uint64_t& OutAccelSize,
	std::uint64_t& OutScratchSize)
{
	if (!bRayTracingSupported || GetAccelerationStructureBuildSizesKHR == nullptr || Desc.Geometries.empty())
	{
		return false;
	}

	std::vector<VkAccelerationStructureGeometryKHR> Geometries;
	Geometries.reserve(Desc.Geometries.size());
	for (const FRHIRayTracingGeometry& Geometry : Desc.Geometries)
	{
		VkAccelerationStructureGeometryKHR Geo{};
		Geo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
		Geo.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
		Geo.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
		Geo.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		Geo.geometry.triangles.vertexStride = Geometry.VertexStride;
		Geo.geometry.triangles.maxVertex = Geometry.VertexCount > 0 ? Geometry.VertexCount - 1 : 0;
		Geometries.push_back(Geo);
	}

	VkAccelerationStructureBuildGeometryInfoKHR BuildInfo{};
	BuildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	BuildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	BuildInfo.flags = Desc.bAllowUpdate
		? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR
		: 0u;
	BuildInfo.geometryCount = static_cast<std::uint32_t>(Geometries.size());
	BuildInfo.pGeometries = Geometries.data();

	std::vector<std::uint32_t> PrimitiveCounts;
	PrimitiveCounts.reserve(Desc.Geometries.size());
	for (const FRHIRayTracingGeometry& Geometry : Desc.Geometries)
	{
		PrimitiveCounts.push_back(
			Geometry.IndexCount > 0 ? Geometry.IndexCount / 3 : Geometry.VertexCount / 3);
	}

	VkAccelerationStructureBuildSizesInfoKHR Sizes{};
	Sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	GetAccelerationStructureBuildSizesKHR(
		Device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&BuildInfo,
		PrimitiveCounts.data(),
		&Sizes);

	OutAccelSize = Sizes.accelerationStructureSize;
	OutScratchSize = Sizes.buildScratchSize;
	return true;
}

FRHIBuffer* FVulkanRHI::CreateShaderBindingTable(
	FRHIRayTracingPipeline* Pipeline,
	const FRHISbtGroup* Groups,
	std::uint32_t GroupCount,
	std::uint32_t* OutRayGenOffset,
	std::uint32_t* OutRayGenStride,
	std::uint32_t* OutHitOffset,
	std::uint32_t* OutHitStride,
	std::uint32_t* OutMissOffset,
	std::uint32_t* OutMissStride)
{
	if (!bRayTracingSupported || GetRayTracingShaderGroupHandlesKHR == nullptr)
	{
		MAHO_LOG_CORE_ERROR("FVulkanRHI::CreateShaderBindingTable: ray tracing unavailable");
		return nullptr;
	}
	if (Pipeline == nullptr || Groups == nullptr || GroupCount == 0)
	{
		MAHO_LOG_CORE_ERROR("FVulkanRHI::CreateShaderBindingTable: invalid args");
		return nullptr;
	}

	auto* VkPipeline = static_cast<FVulkanRayTracingPipeline*>(Pipeline);

	// -- per-stage group count (raygen 1, miss N, hit N) --
	std::uint32_t RayGenCount = 0;
	std::uint32_t HitCount = 0;
	std::uint32_t MissCount = 0;
	for (std::uint32_t G = 0; G < GroupCount; ++G)
	{
		const ERHIShaderStage Stage = Groups[G].Stage;
		if (Stage == ERHIShaderStage::RayGen)
		{
			RayGenCount = static_cast<std::uint32_t>(Groups[G].Records.size());
		}
		else if (Stage == ERHIShaderStage::ClosestHit || Stage == ERHIShaderStage::AnyHit)
		{
			HitCount += static_cast<std::uint32_t>(Groups[G].Records.size());
		}
		else if (Stage == ERHIShaderStage::Miss)
		{
			MissCount += static_cast<std::uint32_t>(Groups[G].Records.size());
		}
	}

	// -- query the pipeline's total shader group count + handle size --
	// The pipeline groups order (as created): raygen(1) + miss(N) + hit(N).
	// We query one handle per pipeline group - the SBT record count must match
	// the pipeline's group count for vkCmdTraceRaysKHR indexing.
	const std::uint32_t TotalGroups = 1 + MissCount + HitCount;
	const std::uint32_t HandleSize = 32; // VK_RAY_TRACING_SHADER_GROUP_HANDLE_SIZE_KHR
	const std::uint32_t HandleAligned = (HandleSize + 15) & ~15u;

	// -- offsets: raygen 0, miss after, hit after --
	const std::uint32_t RayGenOffset = 0;
	const std::uint32_t MissOffset = HandleAligned * RayGenCount;
	const std::uint32_t HitOffset = MissOffset + HandleAligned * MissCount;
	const std::uint32_t TotalSize = HitOffset + HandleAligned * HitCount;

	if (OutRayGenOffset) *OutRayGenOffset = RayGenOffset;
	if (OutRayGenStride) *OutRayGenStride = HandleAligned;
	if (OutMissOffset) *OutMissOffset = MissOffset;
	if (OutMissStride) *OutMissStride = HandleAligned;
	if (OutHitOffset) *OutHitOffset = HitOffset;
	if (OutHitStride) *OutHitStride = HandleAligned;

	// -- SBT storage buffer: CPU->GPU, device-address capable --
	FRHIBufferDesc BufDesc;
	BufDesc.Size = TotalSize;
	BufDesc.Usage = ERHIBufferUsage::DeviceAddress | ERHIBufferUsage::TransferDst;
	BufDesc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
	FRHIBuffer* Sbt = CreateBuffer(BufDesc);
	if (Sbt == nullptr)
	{
		MAHO_LOG_CORE_ERROR("FVulkanRHI::CreateShaderBindingTable: failed to create SBT buffer");
		return nullptr;
	}

	auto* VkSbt = static_cast<FVulkanBuffer*>(Sbt);

	FRHIMemoryAllocation MemAlloc;
	MemAlloc.Native = VkSbt->GetAllocation();
	MemAlloc.Mapped = nullptr;
	void* Mapped = MemoryAllocator->Map(MemAlloc);
	if (Mapped == nullptr)
	{
		DestroyBuffer(Sbt);
		return nullptr;
	}
	std::memset(Mapped, 0, TotalSize);

	// -- fetch each pipeline group's handle, write into the SBT --
	std::vector<std::uint8_t> Handles(TotalGroups * HandleSize);
	if (!CheckVkResult(
			GetRayTracingShaderGroupHandlesKHR(
				Device, VkPipeline->GetVkPipeline(), 0, TotalGroups,
				static_cast<std::uint32_t>(Handles.size()), Handles.data()),
			"vkGetRayTracingShaderGroupHandlesKHR"))
	{
		MemoryAllocator->Unmap(MemAlloc);
		DestroyBuffer(Sbt);
		return nullptr;
	}

	auto WriteRecord = [&](std::uint32_t SbtOffset, std::uint32_t Index)
	{
		std::uint8_t* Dst = static_cast<std::uint8_t*>(Mapped) + SbtOffset;
		std::memcpy(Dst, Handles.data() + static_cast<std::size_t>(Index) * HandleSize, HandleSize);
	};

	// Raygen group = index 0; miss groups follow; hit groups last.
	std::uint32_t HandleIndex = 0;
	WriteRecord(RayGenOffset, HandleIndex++);
	for (std::uint32_t I = 0; I < MissCount; ++I)
	{
		WriteRecord(MissOffset + I * HandleAligned, HandleIndex++);
	}
	for (std::uint32_t I = 0; I < HitCount; ++I)
	{
		WriteRecord(HitOffset + I * HandleAligned, HandleIndex++);
	}

	MemoryAllocator->Unmap(MemAlloc);
	MAHO_LOG_CORE_INFO("FVulkanRHI: created SBT ({} bytes)", TotalSize);
	return Sbt;
}

FRHIStructuredBuffer* FVulkanRHI::CreateStructuredBuffer(const FRHIStructuredBufferDesc& Desc)
{
	if (Desc.Stride == 0 || Desc.Size == 0)
	{
		MAHO_LOG_CORE_ERROR("FVulkanRHI::CreateStructuredBuffer: invalid stride or size");
		return nullptr;
	}

	FRHIBufferDesc BufDesc;
	BufDesc.Size = Desc.Size;
	BufDesc.Usage = Desc.Usage;
	BufDesc.MemoryUsage = Desc.MemoryUsage;
	FRHIBuffer* Underlying = CreateBuffer(BufDesc);
	if (Underlying == nullptr)
	{
		MAHO_LOG_CORE_ERROR("FVulkanRHI::CreateStructuredBuffer: failed to create underlying buffer");
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
		MAHO_LOG_CORE_ERROR("FVulkanRHI::CreateBufferView: null buffer");
		return nullptr;
	}

	auto* Buf = static_cast<FVulkanBuffer*>(Desc.Buffer);
	VkFormat VkFmt = ToVkFormat(Desc.Format);
	if (VkFmt == VK_FORMAT_UNDEFINED)
	{
		MAHO_LOG_CORE_ERROR("FVulkanRHI::CreateBufferView: unsupported format");
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
	Info.subresourceRange.aspectMask = GetImageAspectForFormat(Desc.Format);
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

FRHIQueryPool* FVulkanRHI::CreateQueryPool(ERHIQueryType Type, std::uint32_t QueryCount)
{
	if (Device == VK_NULL_HANDLE || QueryCount == 0)
	{
		return nullptr;
	}

	VkQueryPoolCreateInfo Info{};
	Info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	Info.queryType = (Type == ERHIQueryType::Occlusion) ? VK_QUERY_TYPE_OCCLUSION : VK_QUERY_TYPE_TIMESTAMP;
	Info.queryCount = QueryCount;

	VkQueryPool Pool = VK_NULL_HANDLE;
	if (!CheckVkResult(vkCreateQueryPool(Device, &Info, nullptr, &Pool), "vkCreateQueryPool"))
	{
		return nullptr;
	}
	return new FVulkanQueryPool(Device, Pool);
}

void FVulkanRHI::DestroyQueryPool(FRHIQueryPool* Pool)
{
	delete Pool;
}

bool FVulkanRHI::GetQueryPoolResults(
	FRHIQueryPool* Pool,
	std::uint32_t FirstQuery,
	std::uint32_t QueryCount,
	std::uint64_t* Results,
	std::size_t Stride,
	bool bWait)
{
	if (Pool == nullptr || Results == nullptr || QueryCount == 0)
	{
		return false;
	}

	auto* VkPool = static_cast<FVulkanQueryPool*>(Pool);
	const VkQueryResultFlags Flags = VK_QUERY_RESULT_64_BIT | (bWait ? VK_QUERY_RESULT_WAIT_BIT : 0);
	const VkResult Result = vkGetQueryPoolResults(
		Device,
		VkPool->GetVkQueryPool(),
		FirstQuery,
		QueryCount,
		static_cast<std::size_t>(Stride) * QueryCount,
		Results,
		Stride,
		Flags);
	if (Result != VK_SUCCESS)
	{
		if (Result == VK_NOT_READY && !bWait)
		{
			return false;
		}
		MAHO_LOG_CORE_ERROR("FVulkanRHI::GetQueryPoolResults failed ({})", static_cast<int>(Result));
		return false;
	}
	return true;
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

	// The image count may have changed on recreation -- rebuild the per-image
	// render-finished semaphores to match.
	if (!CreateRenderFinishedSemaphores())
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

	MAHO_LOG_CORE_INFO("Vulkan swapchain recreated ({}x{})", SwapchainExtent.width, SwapchainExtent.height);
	return true;
}

IDynamicRHI* FRHIFactory::Create(ERHIBackend Backend)
{
	switch (Backend)
	{
	case ERHIBackend::Vulkan:
	{
		return new FVulkanRHI();
	}
	}

	MAHO_LOG_CORE_ERROR("FRHIFactory::Create: unsupported ERHIBackend ({})", static_cast<std::uint32_t>(Backend));
	return nullptr;
}

std::uint32_t FVulkanRHI::GetFramebufferWidth() const
{
	return SwapchainExtent.width;
}

std::uint32_t FVulkanRHI::GetFramebufferHeight() const
{
	return SwapchainExtent.height;
}

} // namespace Maho
