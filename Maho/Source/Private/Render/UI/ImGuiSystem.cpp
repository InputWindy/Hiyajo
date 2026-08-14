#include <Render/UI/ImGuiSystem.h>

#include <Core/System/Console.h>
#include <Core/System/Log.h>
#include <Core/System/PlatformWindow.h>
#include <Core/System/Utf8Path.h>
#include <Render/RHI/RHIResourceManager.h>
#include <Render/RHI/RHIServer.h>
#include <Render/UI/ImGuiTheme.h>
#include "Render/RHI/VulkanResources.h"
#include "Render/RHI/VulkanRHI.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <ImGuizmo.h>
#include <implot.h>

#include <GLFW/glfw3.h>

#include <atomic>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace Maho
{

namespace
{

static TAutoConsoleVariable GCVarImGuiDescriptorPoolSize(
	"r.ImGui.DescriptorPoolSize",
	64,
	"ImGui Vulkan descriptor pool size (font + ImGui_ImplVulkan_AddTexture slots)");

std::atomic<std::uint64_t> GImGuiRgba8TextureSerial{ 1 };

/** Viewport Renderer_* hooks that touch Vulkan — run on MahoRHI while Game waits (Flush). */
FRHIServer* GImGuiViewportRHIServer = nullptr;
void (*GImGuiOrig_Renderer_CreateWindow)(ImGuiViewport*) = nullptr;
void (*GImGuiOrig_Renderer_DestroyWindow)(ImGuiViewport*) = nullptr;
void (*GImGuiOrig_Renderer_SetWindowSize)(ImGuiViewport*, ImVec2) = nullptr;

void Maho_ImGui_Renderer_CreateWindow(ImGuiViewport* Viewport)
{
	if (!GImGuiViewportRHIServer || !GImGuiOrig_Renderer_CreateWindow)
	{
		return;
	}
	GImGuiViewportRHIServer->Enqueue(
		[Viewport](FThreadedServer& /*Server*/)
		{
			GImGuiOrig_Renderer_CreateWindow(Viewport);
		});
	GImGuiViewportRHIServer->Flush();
}

void Maho_ImGui_Renderer_DestroyWindow(ImGuiViewport* Viewport)
{
	if (!GImGuiViewportRHIServer || !GImGuiOrig_Renderer_DestroyWindow)
	{
		return;
	}
	GImGuiViewportRHIServer->Enqueue(
		[Viewport](FThreadedServer& /*Server*/)
		{
			GImGuiOrig_Renderer_DestroyWindow(Viewport);
		});
	GImGuiViewportRHIServer->Flush();
}

void Maho_ImGui_Renderer_SetWindowSize(ImGuiViewport* Viewport, ImVec2 Size)
{
	if (!GImGuiViewportRHIServer || !GImGuiOrig_Renderer_SetWindowSize)
	{
		return;
	}
	GImGuiViewportRHIServer->Enqueue(
		[Viewport, Size](FThreadedServer& /*Server*/)
		{
			GImGuiOrig_Renderer_SetWindowSize(Viewport, Size);
		});
	GImGuiViewportRHIServer->Flush();
}

void CheckImGuiVkResult(VkResult Result)
{
	if (Result == 0)
	{
		return;
	}
	MAHO_CORE_ERROR("ImGui Vulkan error: VkResult = {}", static_cast<int>(Result));
}

/** Directory that contains Maho.dll / the game exe (fonts are copied next to binaries). */
[[nodiscard]] std::filesystem::path GetBinaryDirectory()
{
#if defined(_WIN32)
	HMODULE Module = nullptr;
	if (GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&GetBinaryDirectory),
			&Module) &&
		Module != nullptr)
	{
		wchar_t Buffer[MAX_PATH]{};
		const DWORD Length = GetModuleFileNameW(Module, Buffer, MAX_PATH);
		if (Length > 0 && Length < MAX_PATH)
		{
			return std::filesystem::path(Buffer).parent_path();
		}
	}
#endif
	return std::filesystem::current_path();
}

/** Prefer binary-adjacent Engine/Fonts, then cwd / Config fallbacks. */
[[nodiscard]] std::filesystem::path ResolveFontPath(
	const std::filesystem::path& FontFile,
	const std::filesystem::path& BinaryDir,
	const std::string& ConfigDirectory)
{
	namespace fs = std::filesystem;
	const fs::path Candidates[] = {
		BinaryDir / "Engine" / "Fonts" / FontFile,
		fs::current_path() / "Engine" / "Fonts" / FontFile,
		fs::path(ConfigDirectory) / ".." / "Engine" / "Fonts" / FontFile,
		fs::path(ConfigDirectory) / "Fonts" / FontFile,
	};
	for (const fs::path& Candidate : Candidates)
	{
		const fs::path Normalized = Candidate.lexically_normal();
		std::error_code ErrorCode;
		if (fs::exists(Normalized, ErrorCode) && !ErrorCode)
		{
			return Normalized;
		}
	}
	return {};
}

[[nodiscard]] ImFont* TryAddUiFont(ImGuiIO& IO, const std::filesystem::path& FontPath, float SizePixels)
{
	std::error_code ErrorCode;
	if (FontPath.empty() || !std::filesystem::exists(FontPath, ErrorCode) || ErrorCode)
	{
		return nullptr;
	}

	ImFontConfig Config;
	Config.OversampleH = 2;
	Config.OversampleV = 1;
	Config.PixelSnapH = true;
	const std::string Utf8Path = PathToUtf8(FontPath);
	return IO.Fonts->AddFontFromFileTTF(Utf8Path.c_str(), SizePixels, &Config);
}

[[nodiscard]] bool TryMergeFontAwesome(ImGuiIO& IO, const std::filesystem::path& FontPath, float SizePixels)
{
	std::error_code ErrorCode;
	if (FontPath.empty() || !std::filesystem::exists(FontPath, ErrorCode) || ErrorCode)
	{
		return false;
	}

	// Font Awesome glyphs need ~2/3 of the UI size to optically align with Latin text.
	ImFontConfig IconsConfig;
	IconsConfig.MergeMode = true;
	IconsConfig.PixelSnapH = true;
	IconsConfig.GlyphMinAdvanceX = SizePixels;
	static const ImWchar IconRanges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
	const std::string Utf8Path = PathToUtf8(FontPath);
	return IO.Fonts->AddFontFromFileTTF(
		Utf8Path.c_str(),
		SizePixels,
		&IconsConfig,
		IconRanges) != nullptr;
}

#if defined(_WIN32)
[[nodiscard]] bool TryMergeSystemCjkFont(
	ImGuiIO& IO,
	const wchar_t* WideFontPath,
	float SizePixels,
	const ImWchar* GlyphRanges,
	const char* LogLabel)
{
	namespace fs = std::filesystem;
	std::error_code ErrorCode;
	const fs::path FontPath(WideFontPath);
	if (!fs::exists(FontPath, ErrorCode) || ErrorCode)
	{
		return false;
	}

	ImFontConfig Config;
	Config.MergeMode = true;
	Config.PixelSnapH = true;
	Config.OversampleH = 1;
	Config.OversampleV = 1;
	// .ttc collections: face 0 is the regular UI face for YaHei / Yu Gothic.
	Config.FontNo = 0;

	const std::string Utf8Path = PathToUtf8(FontPath);
	if (!IO.Fonts->AddFontFromFileTTF(Utf8Path.c_str(), SizePixels, &Config, GlyphRanges))
	{
		return false;
	}
	MAHO_CORE_INFO("FImGuiSystem: merged {} glyphs from '{}'", LogLabel, Utf8Path);
	return true;
}

void MergeEditorCjkFonts(ImGuiIO& IO, float SizePixels)
{
	bool bMergedChinese = false;
	const wchar_t* ChineseCandidates[] = {
		L"C:\\Windows\\Fonts\\msyh.ttc",
		L"C:\\Windows\\Fonts\\msyh.ttf",
		L"C:\\Windows\\Fonts\\msyhbd.ttc",
		L"C:\\Windows\\Fonts\\simhei.ttf",
	};
	for (const wchar_t* Candidate : ChineseCandidates)
	{
		if (TryMergeSystemCjkFont(
				IO,
				Candidate,
				SizePixels,
				IO.Fonts->GetGlyphRangesChineseFull(),
				"Chinese"))
		{
			bMergedChinese = true;
			break;
		}
	}

	bool bMergedJapanese = false;
	const wchar_t* JapaneseCandidates[] = {
		L"C:\\Windows\\Fonts\\YuGothR.ttc",
		L"C:\\Windows\\Fonts\\YuGothM.ttc",
		L"C:\\Windows\\Fonts\\meiryo.ttc",
		L"C:\\Windows\\Fonts\\msgothic.ttc",
	};
	for (const wchar_t* Candidate : JapaneseCandidates)
	{
		if (TryMergeSystemCjkFont(
				IO,
				Candidate,
				SizePixels,
				IO.Fonts->GetGlyphRangesJapanese(),
				"Japanese"))
		{
			bMergedJapanese = true;
			break;
		}
	}

	if (!bMergedChinese)
	{
		MAHO_CORE_WARN("FImGuiSystem: no Chinese UI font found under Windows\\Fonts");
	}
	if (!bMergedJapanese)
	{
		MAHO_CORE_WARN("FImGuiSystem: no Japanese UI font found under Windows\\Fonts");
	}
}
#endif

void LoadEditorFonts(ImGuiIO& IO, const std::string& ConfigDirectory)
{
	namespace fs = std::filesystem;
	constexpr float kUiFontSize = 16.0f;
	constexpr float kIconFontSize = kUiFontSize * 2.0f / 3.0f;

	const fs::path BinaryDir = GetBinaryDirectory();
	ImFont* UiFont = nullptr;

	// Primary: Inter (OFL). Fallback: Roboto Medium (Apache 2.0, shipped with imgui samples).
	const char* UiFontCandidates[] = {
		"Inter-Regular.ttf",
		"Roboto-Medium.ttf",
	};
	for (const char* FontFile : UiFontCandidates)
	{
		const fs::path Path = ResolveFontPath(FontFile, BinaryDir, ConfigDirectory);
		UiFont = TryAddUiFont(IO, Path, kUiFontSize);
		if (UiFont)
		{
			MAHO_CORE_INFO(
				"FImGuiSystem: loaded UI font '{}' @ {:.0f}px",
				PathToUtf8(Path),
				kUiFontSize);
			break;
		}
	}

#if defined(_WIN32)
	// Last resort on developer machines if Engine/Fonts was not copied.
	if (!UiFont)
	{
		const fs::path Segoe = fs::path(L"C:\\Windows\\Fonts\\segoeui.ttf");
		UiFont = TryAddUiFont(IO, Segoe, kUiFontSize);
		if (UiFont)
		{
			MAHO_CORE_INFO("FImGuiSystem: loaded UI font '{}'", PathToUtf8(Segoe));
		}
	}
#endif

	if (!UiFont)
	{
		UiFont = IO.Fonts->AddFontDefault();
		MAHO_CORE_WARN("FImGuiSystem: UI font missing; using Proggy default");
	}

#if defined(_WIN32)
	// Merge CJK after Latin base so Content Browser can show Chinese/Japanese names.
	MergeEditorCjkFonts(IO, kUiFontSize);
#endif

	const fs::path IconPath = ResolveFontPath(FONT_ICON_FILE_NAME_FAS, BinaryDir, ConfigDirectory);
	if (TryMergeFontAwesome(IO, IconPath, kIconFontSize))
	{
		MAHO_CORE_INFO("FImGuiSystem: merged icon font '{}'", PathToUtf8(IconPath));
	}
	else
	{
		MAHO_CORE_WARN(
			"FImGuiSystem: Font Awesome not found (expected Engine/Fonts/{} next to binary or cwd)",
			FONT_ICON_FILE_NAME_FAS);
	}

	IO.FontDefault = UiFont;
}

} // namespace

FImGuiSystem::~FImGuiSystem()
{
	// Prefer Shutdown(RHIServer) from FRenderSystem so Vulkan backends die before the device.
}

bool FImGuiSystem::Initialize(
	FPlatformWindow& Window,
	FRHIServer& RHIServer,
	const std::string& ConfigDirectory)
{
	if (bInitialized)
	{
		return true;
	}

	if (!Window.HasOsWindow())
	{
		MAHO_CORE_INFO("FImGuiSystem: skipped (no OS window)");
		return false;
	}

	void* ToolkitHandle = Window.GetToolkitWindowHandle();
	if (!ToolkitHandle)
	{
		MAHO_CORE_ERROR("FImGuiSystem::Initialize: toolkit window handle is null");
		return false;
	}

	FVulkanRHI* VulkanRHI = RHIServer.GetVulkanRHI();
	if (!VulkanRHI || !VulkanRHI->IsInitialized())
	{
		MAHO_CORE_ERROR("FImGuiSystem::Initialize: Vulkan RHI is not ready");
		return false;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImPlot::CreateContext();
	ImGuiIO& IO = ImGui::GetIO();
	IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	IO.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

	{
		namespace fs = std::filesystem;
		std::error_code ErrorCode;
		const fs::path ConfigDir = ConfigDirectory.empty() ? fs::path("Config") : fs::path(ConfigDirectory);
		fs::create_directories(ConfigDir, ErrorCode);
		IniFilePath = (ConfigDir / "imgui.ini").string();
		IO.IniFilename = IniFilePath.c_str();
		MAHO_CORE_INFO("FImGuiSystem: ini path '{}'", IniFilePath);
	}

	LoadEditorFonts(IO, ConfigDirectory);

	ImGui::StyleColorsDark();
	ApplyMahoNightTheme();
	if (IO.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		// Keep theme WindowBg alpha (desktop wallpaper shows through docked panels).
		ImGuiStyle& Style = ImGui::GetStyle();
		Style.WindowRounding = 0.0f;
	}

	GLFWwindow* GlfwWindow = static_cast<GLFWwindow*>(ToolkitHandle);
	if (!ImGui_ImplGlfw_InitForVulkan(GlfwWindow, true))
	{
		MAHO_CORE_ERROR("FImGuiSystem::Initialize: ImGui_ImplGlfw_InitForVulkan failed");
		ImPlot::DestroyContext();
		ImGui::DestroyContext();
		return false;
	}

	std::atomic<bool> bVulkanBackendOk{false};
	RHIServer.Enqueue([VulkanRHI, &bVulkanBackendOk](FThreadedServer& /*Server*/)
	{
		ImGui_ImplVulkan_InitInfo InitInfo{};
		InitInfo.ApiVersion = VK_API_VERSION_1_2;
		InitInfo.Instance = VulkanRHI->GetVkInstance();
		InitInfo.PhysicalDevice = VulkanRHI->GetVkPhysicalDevice();
		InitInfo.Device = VulkanRHI->GetVkDevice();
		InitInfo.QueueFamily = VulkanRHI->GetGraphicsQueueFamilyIndex();
		InitInfo.Queue = VulkanRHI->GetVkGraphicsQueue();
		InitInfo.DescriptorPool = VK_NULL_HANDLE;
		InitInfo.DescriptorPoolSize = static_cast<std::uint32_t>(
			(std::max)(1, GCVarImGuiDescriptorPoolSize.GetValue()));
		InitInfo.RenderPass = VulkanRHI->GetVkRenderPass();
		InitInfo.MinImageCount = VulkanRHI->GetMinImageCount();
		InitInfo.ImageCount = VulkanRHI->GetSwapchainImageCount();
		InitInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		InitInfo.Subpass = 0;
		InitInfo.CheckVkResultFn = CheckImGuiVkResult;

		bVulkanBackendOk.store(ImGui_ImplVulkan_Init(&InitInfo));
	});
	RHIServer.Flush();

	if (!bVulkanBackendOk.load())
	{
		MAHO_CORE_ERROR("FImGuiSystem::Initialize: ImGui_ImplVulkan_Init failed");
		ImGui_ImplGlfw_Shutdown();
		ImPlot::DestroyContext();
		ImGui::DestroyContext();
		return false;
	}

	InstallViewportRHIMarshaling(RHIServer);

	bInitialized = true;
	MAHO_CORE_INFO("FImGuiSystem initialized (GLFW + Vulkan, docking + viewports)");
	return true;
}

void FImGuiSystem::Shutdown(FRHIServer& RHIServer)
{
	if (!bInitialized)
	{
		return;
	}

	RHIServer.Flush();
	DestroyAllTextures(RHIServer);

	UninstallViewportRHIMarshaling();

	RHIServer.Enqueue([](FThreadedServer& /*Server*/)
	{
		ImGui_ImplVulkan_Shutdown();
	});
	RHIServer.Flush();

	ImGui_ImplGlfw_Shutdown();
	ImPlot::DestroyContext();
	ImGui::DestroyContext();

	IniFilePath.clear();
	bInitialized = false;
	MAHO_CORE_INFO("FImGuiSystem shut down");
}

void FImGuiSystem::BeginFrame()
{
	if (!bInitialized)
	{
		return;
	}

	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
	ImGuizmo::BeginFrame();
}

void FImGuiSystem::EndFrame()
{
	if (!bInitialized)
	{
		return;
	}

	ImGui::Render();
}

void FImGuiSystem::UpdatePlatformWindows()
{
	if (!bInitialized)
	{
		return;
	}

	const ImGuiIO& IO = ImGui::GetIO();
	if ((IO.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) == 0)
	{
		return;
	}

	// GLFW Platform_* on game thread; Vulkan Renderer_Create/Destroy/SetWindowSize
	// marshaled to MahoRHI (see InstallViewportRHIMarshaling).
	ImGui::UpdatePlatformWindows();
}

void FImGuiSystem::InstallViewportRHIMarshaling(FRHIServer& RHIServer)
{
	ImGuiPlatformIO& PlatformIO = ImGui::GetPlatformIO();
	GImGuiViewportRHIServer = &RHIServer;
	GImGuiOrig_Renderer_CreateWindow = PlatformIO.Renderer_CreateWindow;
	GImGuiOrig_Renderer_DestroyWindow = PlatformIO.Renderer_DestroyWindow;
	GImGuiOrig_Renderer_SetWindowSize = PlatformIO.Renderer_SetWindowSize;
	if (GImGuiOrig_Renderer_CreateWindow)
	{
		PlatformIO.Renderer_CreateWindow = Maho_ImGui_Renderer_CreateWindow;
	}
	if (GImGuiOrig_Renderer_DestroyWindow)
	{
		PlatformIO.Renderer_DestroyWindow = Maho_ImGui_Renderer_DestroyWindow;
	}
	if (GImGuiOrig_Renderer_SetWindowSize)
	{
		PlatformIO.Renderer_SetWindowSize = Maho_ImGui_Renderer_SetWindowSize;
	}
}

void FImGuiSystem::UninstallViewportRHIMarshaling()
{
	if (ImGui::GetCurrentContext() != nullptr)
	{
		ImGuiPlatformIO& PlatformIO = ImGui::GetPlatformIO();
		if (GImGuiOrig_Renderer_CreateWindow)
		{
			PlatformIO.Renderer_CreateWindow = GImGuiOrig_Renderer_CreateWindow;
		}
		if (GImGuiOrig_Renderer_DestroyWindow)
		{
			PlatformIO.Renderer_DestroyWindow = GImGuiOrig_Renderer_DestroyWindow;
		}
		if (GImGuiOrig_Renderer_SetWindowSize)
		{
			PlatformIO.Renderer_SetWindowSize = GImGuiOrig_Renderer_SetWindowSize;
		}
	}
	GImGuiOrig_Renderer_CreateWindow = nullptr;
	GImGuiOrig_Renderer_DestroyWindow = nullptr;
	GImGuiOrig_Renderer_SetWindowSize = nullptr;
	GImGuiViewportRHIServer = nullptr;
}

bool FImGuiSystem::PollExitRequest() const
{
	if (!bInitialized)
	{
		return false;
	}

	const ImGuiIO& IO = ImGui::GetIO();
	return !IO.WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_Escape);
}

void FImGuiSystem::DestroyTextureOnRHI(FRHIServer& RHIServer, FOwnedGpuTexture& Owned)
{
	IRHI* RHI = RHIServer.GetRHI();
	FVulkanRHI* VulkanRHI = RHIServer.GetVulkanRHI();
	if (!RHI || !VulkanRHI)
	{
		Owned = {};
		return;
	}

	if (Owned.DescriptorSet)
	{
		ImGui_ImplVulkan_RemoveTexture(static_cast<VkDescriptorSet>(Owned.DescriptorSet));
		Owned.DescriptorSet = nullptr;
	}
	if (Owned.bOwnsImageView && Owned.ImageView)
	{
		vkDestroyImageView(VulkanRHI->GetVkDevice(), static_cast<VkImageView>(Owned.ImageView), nullptr);
		Owned.ImageView = nullptr;
	}
	else
	{
		Owned.ImageView = nullptr;
	}
	if (Owned.Sampler)
	{
		RHI->GetResourceManager().Release(Owned.Sampler, true);
		Owned.Sampler = nullptr;
	}
	if (Owned.bOwnsTexture && Owned.Texture)
	{
		RHI->GetResourceManager().Release(Owned.Texture, true);
		Owned.Texture = nullptr;
	}
	else
	{
		Owned.Texture = nullptr;
	}
}

void FImGuiSystem::DestroyAllTextures(FRHIServer& RHIServer)
{
	if (OwnedTextures.empty())
	{
		return;
	}

	RHIServer.Enqueue(
		[this, &RHIServer](FThreadedServer& /*Server*/)
		{
			for (auto& Pair : OwnedTextures)
			{
				DestroyTextureOnRHI(RHIServer, Pair.second);
			}
			OwnedTextures.clear();
		});
	RHIServer.Flush();
}

void FImGuiSystem::DestroyTexture(FRHIServer& RHIServer, FImGuiTextureHandle& Handle)
{
	if (!Handle.IsValid())
	{
		return;
	}

	const auto It = OwnedTextures.find(Handle.Id);
	if (It == OwnedTextures.end())
	{
		Handle.Reset();
		return;
	}

	FOwnedGpuTexture Owned = It->second;
	OwnedTextures.erase(It);
	Handle.Reset();

	RHIServer.Enqueue(
		[this, &RHIServer, Owned](FThreadedServer& /*Server*/) mutable
		{
			DestroyTextureOnRHI(RHIServer, Owned);
		});
	RHIServer.Flush();
}

bool FImGuiSystem::CreateRgba8Texture(
	FRHIServer& RHIServer,
	std::uint32_t Width,
	std::uint32_t Height,
	const std::uint8_t* Pixels,
	std::size_t PixelByteCount,
	FImGuiTextureHandle& OutHandle)
{
	OutHandle.Reset();
	if (!bInitialized || !Pixels || Width == 0 || Height == 0)
	{
		return false;
	}

	const std::size_t Expected =
		static_cast<std::size_t>(Width) * static_cast<std::size_t>(Height) * 4u;
	if (PixelByteCount < Expected)
	{
		MAHO_CORE_ERROR(
			"FImGuiSystem::CreateRgba8Texture: pixel buffer too small ({} < {})",
			PixelByteCount,
			Expected);
		return false;
	}

	if (!RHIServer.HasRHI() || !RHIServer.GetVulkanRHI())
	{
		MAHO_CORE_ERROR("FImGuiSystem::CreateRgba8Texture: Vulkan RHI unavailable");
		return false;
	}

	std::vector<std::uint8_t> PixelCopy(Pixels, Pixels + Expected);
	FOwnedGpuTexture Created{};
	std::atomic<bool> bOk{false};

	RHIServer.Enqueue(
		[this, &RHIServer, Width, Height, PixelCopy = std::move(PixelCopy), &Created, &bOk](
			FThreadedServer& /*Server*/) mutable
		{
			IRHI* RHI = RHIServer.GetRHI();
			FVulkanRHI* VulkanRHI = RHIServer.GetVulkanRHI();
			if (!RHI || !VulkanRHI)
			{
				return;
			}

			FRHITextureDesc Desc{};
			Desc.Format = ERHIFormat::R8G8B8A8_UNORM;
			Desc.Dimension = ERHITextureDimension::Tex2D;
			Desc.Extent = { Width, Height, 1 };
			Desc.MipLevels = 1;
			Desc.ArrayLayers = 1;
			Desc.Usage = ERHITextureUsage::Sampled | ERHITextureUsage::TransferDst;
			Desc.MemoryUsage = ERHIMemoryUsage::GPUOnly;

			FRHIResourceManager& Manager = RHI->GetResourceManager();
			// Unique key per upload — a fixed name made wallpaper + preview share one GPU image
			// (Named AcquireTexture returns the existing entry and subsequent copies overwrite it).
			const std::string DebugKey =
				"ImGui.Rgba8." + std::to_string(GImGuiRgba8TextureSerial.fetch_add(1));
			Created.Texture = Manager.AcquireTexture(Desc, DebugKey.c_str());
			if (!Created.Texture)
			{
				MAHO_CORE_ERROR("FImGuiSystem::CreateRgba8Texture: AcquireTexture failed");
				return;
			}

			FRHIBufferDesc StagingDesc{};
			StagingDesc.Size = PixelCopy.size();
			StagingDesc.Usage = ERHIBufferUsage::TransferSrc;
			StagingDesc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
			FRHIBuffer* Staging = Manager.AcquireBuffer(StagingDesc);
			if (!Staging)
			{
				Manager.Release(Created.Texture, true);
				Created.Texture = nullptr;
				MAHO_CORE_ERROR("FImGuiSystem::CreateRgba8Texture: staging buffer failed");
				return;
			}

			RHI->UpdateBuffer(Staging, 0, StagingDesc.Size, PixelCopy.data());

			FRHICommandList* CmdList = RHI->CreateCommandList(ERHICommandListType::Transfer);
			if (!CmdList)
			{
				Manager.Release(Staging, true);
				Manager.Release(Created.Texture, true);
				Created.Texture = nullptr;
				MAHO_CORE_ERROR("FImGuiSystem::CreateRgba8Texture: CreateCommandList failed");
				return;
			}

			CmdList->Begin();
			CmdList->TransitionTexture(Created.Texture, ERHIResourceState::Common, ERHIResourceState::CopyDst);
			CmdList->CopyBufferToTexture(Staging, Created.Texture, 0);
			CmdList->TransitionTexture(
				Created.Texture,
				ERHIResourceState::CopyDst,
				ERHIResourceState::ShaderResource);
			CmdList->End();

			FRHIFence* Fence = RHI->CreateFence(false);
			FRHICommandList* Lists[] = { CmdList };
			RHI->GetTransferQueue().Submit(Lists, 1, nullptr, 0, nullptr, 0, Fence);
			if (Fence)
			{
				RHI->WaitForFence(Fence);
				RHI->DestroyFence(Fence);
			}
			RHI->DestroyCommandList(CmdList);
			Manager.Release(Staging, true);

			auto* VkTex = static_cast<FVulkanTexture*>(Created.Texture);
			VkImageViewCreateInfo ViewInfo{};
			ViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			ViewInfo.image = VkTex->GetVkImage();
			ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			ViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
			ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			ViewInfo.subresourceRange.baseMipLevel = 0;
			ViewInfo.subresourceRange.levelCount = 1;
			ViewInfo.subresourceRange.baseArrayLayer = 0;
			ViewInfo.subresourceRange.layerCount = 1;

			VkImageView ImageView = VK_NULL_HANDLE;
			if (vkCreateImageView(VulkanRHI->GetVkDevice(), &ViewInfo, nullptr, &ImageView) != VK_SUCCESS)
			{
				Manager.Release(Created.Texture, true);
				Created.Texture = nullptr;
				MAHO_CORE_ERROR("FImGuiSystem::CreateRgba8Texture: vkCreateImageView failed");
				return;
			}
			Created.ImageView = ImageView;

			FRHISamplerDesc SamplerDesc{};
			SamplerDesc.MagFilter = ERHIFilter::Linear;
			SamplerDesc.MinFilter = ERHIFilter::Linear;
			SamplerDesc.AddressU = ERHIAddressMode::ClampToEdge;
			SamplerDesc.AddressV = ERHIAddressMode::ClampToEdge;
			SamplerDesc.AddressW = ERHIAddressMode::ClampToEdge;
			Created.Sampler = Manager.AcquireSampler(SamplerDesc);
			if (!Created.Sampler)
			{
				vkDestroyImageView(VulkanRHI->GetVkDevice(), ImageView, nullptr);
				Created.ImageView = nullptr;
				Manager.Release(Created.Texture, true);
				Created.Texture = nullptr;
				MAHO_CORE_ERROR("FImGuiSystem::CreateRgba8Texture: AcquireSampler failed");
				return;
			}

			auto* VkSampler = static_cast<FVulkanSampler*>(Created.Sampler);
			VkDescriptorSet Set = ImGui_ImplVulkan_AddTexture(
				VkSampler->GetVkSampler(),
				ImageView,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			if (!Set)
			{
				DestroyTextureOnRHI(RHIServer, Created);
				MAHO_CORE_ERROR("FImGuiSystem::CreateRgba8Texture: ImGui_ImplVulkan_AddTexture failed");
				return;
			}

			Created.DescriptorSet = Set;
			bOk.store(true);
		});
	RHIServer.Flush();

	if (!bOk.load() || !Created.DescriptorSet)
	{
		return false;
	}

	OutHandle.Id = Created.DescriptorSet;
	OwnedTextures.emplace(OutHandle.Id, Created);
	return true;
}

bool FImGuiSystem::RegisterExternalSampledTexture(
	FRHIServer& RHIServer,
	FRHITextureView* View,
	FImGuiTextureHandle& OutHandle)
{
	OutHandle.Reset();
	if (!bInitialized || !View)
	{
		return false;
	}

	if (!RHIServer.HasRHI() || !RHIServer.GetVulkanRHI())
	{
		MAHO_CORE_ERROR("FImGuiSystem::RegisterExternalSampledTexture: Vulkan RHI unavailable");
		return false;
	}

	FOwnedGpuTexture Created{};
	Created.bOwnsTexture = false;
	Created.bOwnsImageView = false;
	std::atomic<bool> bOk{false};

	RHIServer.Enqueue(
		[this, &RHIServer, View, &Created, &bOk](FThreadedServer& /*Server*/)
		{
			IRHI* RHI = RHIServer.GetRHI();
			if (!RHI)
			{
				return;
			}

			auto* VkView = static_cast<FVulkanTextureView*>(View);
			if (!VkView || VkView->GetVkImageView() == VK_NULL_HANDLE)
			{
				MAHO_CORE_ERROR("FImGuiSystem::RegisterExternalSampledTexture: invalid texture view");
				return;
			}

			FRHISamplerDesc SamplerDesc{};
			SamplerDesc.MagFilter = ERHIFilter::Linear;
			SamplerDesc.MinFilter = ERHIFilter::Linear;
			SamplerDesc.AddressU = ERHIAddressMode::ClampToEdge;
			SamplerDesc.AddressV = ERHIAddressMode::ClampToEdge;
			SamplerDesc.AddressW = ERHIAddressMode::ClampToEdge;
			Created.Sampler = RHI->GetResourceManager().AcquireSampler(SamplerDesc);
			if (!Created.Sampler)
			{
				MAHO_CORE_ERROR("FImGuiSystem::RegisterExternalSampledTexture: AcquireSampler failed");
				return;
			}

			auto* VkSampler = static_cast<FVulkanSampler*>(Created.Sampler);
			VkDescriptorSet Set = ImGui_ImplVulkan_AddTexture(
				VkSampler->GetVkSampler(),
				VkView->GetVkImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			if (!Set)
			{
				DestroyTextureOnRHI(RHIServer, Created);
				MAHO_CORE_ERROR("FImGuiSystem::RegisterExternalSampledTexture: ImGui_ImplVulkan_AddTexture failed");
				return;
			}

			Created.ImageView = VkView->GetVkImageView();
			Created.DescriptorSet = Set;
			bOk.store(true);
		});
	RHIServer.Flush();

	if (!bOk.load() || !Created.DescriptorSet)
	{
		return false;
	}

	OutHandle.Id = Created.DescriptorSet;
	OwnedTextures.emplace(OutHandle.Id, Created);
	return true;
}

} // namespace Maho
