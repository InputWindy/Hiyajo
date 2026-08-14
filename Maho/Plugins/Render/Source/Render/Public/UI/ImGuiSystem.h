#pragma once

#include "RenderApi.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace Maho
{

class FPlatformWindow;
class FRHIServer;
class FRHITexture;
class FRHITextureView;
class FRHISampler;

/** Opaque ImGui texture id (VkDescriptorSet on Vulkan). */
struct FImGuiTextureHandle
{
	void* Id = nullptr;

	[[nodiscard]] bool IsValid() const { return Id != nullptr; }
	void Reset() { Id = nullptr; }
};

/**
 * Dear ImGui lifecycle helper (GLFW + Vulkan backends).
 * Call BeginFrame after PollEvents (Game); build UI in TickGroups; EndFrame inside FRenderSystem Render.
 */
class MAHO_RENDER_API FImGuiSystem
{
public:
	FImGuiSystem() = default;
	~FImGuiSystem();

	FImGuiSystem(const FImGuiSystem&) = delete;
	FImGuiSystem& operator=(const FImGuiSystem&) = delete;

	/**
	 * Requires an OS window + initialized Vulkan RHI on the RHI server.
	 * Safe to call when headless (returns false without error spam).
	 * @param ConfigDirectory Project Config/ path; imgui.ini is stored as ConfigDirectory/imgui.ini.
	 */
	[[nodiscard]] bool Initialize(
		FPlatformWindow& Window,
		FRHIServer& RHIServer,
		const std::string& ConfigDirectory = "Config");

	/** Flushes the RHI server and tears down GLFW/Vulkan backends. */
	void Shutdown(FRHIServer& RHIServer);

	[[nodiscard]] bool IsInitialized() const { return bInitialized; }

	/** ImGui_Impl*_NewFrame + ImGui::NewFrame. */
	void BeginFrame();

	/** ImGui::Render — draw data must be consumed before the next BeginFrame. */
	void EndFrame();

	/**
	 * Secondary OS viewports: GLFW create/move/resize only (game/main thread).
	 * Vulkan create/resize for those windows is marshaled onto MahoRHI (sync Flush).
	 * Per-frame draw/present is submitted via FRHIServer::SubmitRenderPlatformWindows.
	 */
	void UpdatePlatformWindows();

	/** True when Escape was pressed and ImGui is not capturing keyboard input. */
	[[nodiscard]] bool PollExitRequest() const;

	/**
	 * Upload RGBA8 pixels into a Vulkan image and register it with ImGui_ImplVulkan_AddTexture.
	 * Blocks on FRHIServer::Flush. Game-thread call.
	 */
	[[nodiscard]] bool CreateRgba8Texture(
		FRHIServer& RHIServer,
		std::uint32_t Width,
		std::uint32_t Height,
		const std::uint8_t* Pixels,
		std::size_t PixelByteCount,
		FImGuiTextureHandle& OutHandle);

	/**
	 * Bind an existing RHI sampled texture view for ImGui::Image (does not own the GPU image).
	 * Blocks on FRHIServer::Flush. Call DestroyTexture to free the ImGui descriptor + sampler only.
	 */
	[[nodiscard]] bool RegisterExternalSampledTexture(
		FRHIServer& RHIServer,
		FRHITextureView* View,
		FImGuiTextureHandle& OutHandle);

	/** Unregister and free GPU resources for a texture created by CreateRgba8Texture / RegisterExternalSampledTexture. */
	void DestroyTexture(FRHIServer& RHIServer, FImGuiTextureHandle& Handle);

private:
	struct FOwnedGpuTexture
	{
		FRHITexture* Texture = nullptr;
		FRHISampler* Sampler = nullptr;
		void* ImageView = nullptr; // VkImageView (owned only when created by CreateRgba8Texture)
		void* DescriptorSet = nullptr; // VkDescriptorSet / ImTextureID
		bool bOwnsTexture = true;
		bool bOwnsImageView = true;
	};

	void DestroyTextureOnRHI(FRHIServer& RHIServer, FOwnedGpuTexture& Owned);
	void DestroyAllTextures(FRHIServer& RHIServer);
	void InstallViewportRHIMarshaling(FRHIServer& RHIServer);
	void UninstallViewportRHIMarshaling();

	bool bInitialized = false;
	/** Owns storage for ImGuiIO::IniFilename (ImGui keeps a raw const char*). */
	std::string IniFilePath;
	std::unordered_map<void*, FOwnedGpuTexture> OwnedTextures;
};

} // namespace Maho
