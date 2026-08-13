#pragma once

#include <Core/Engine.h>
#include <Core/Export.h>
#include <Render/ResourceSnapshots.h>
#include <Core/Server/ThreadedServer.h>
#include <Core/Server/TransferHandle.h>
#include <Core/System/PlatformWindow.h>
#include <Render/RenderFramePacket.h>
#include <Render/RHI/RHIServer.h>
#include <Render/Sequencer/RenderFeature.h>
#include <Render/RenderPipelineStage.h>
#include <Render/UI/ImGuiSystem.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Maho
{

class FVulkanRHI;
struct FImGuiDrawDataRing;
class FTextureProxyRegistry;
class FMeshProxyRegistry;
class FSkeletonProxyRegistry;
class FAnimationProxyRegistry;

/** Tag — specializations provide Submit in RenderServer.cpp. */
template <typename TResource>
struct TRenderResourceExporter;

/**
 * MahoRender worker (FThreadedServer). Owned by FRenderSystem (Game).
 * Game submits work via ENQUEUE_RENDER_COMMAND; this server records/submits to FRHIServer.
 */
class MAHO_API FRenderServer final : public FThreadedServer
{
public:
	static constexpr int MaxFramesInFlightCap = 3;

	FRenderServer();
	~FRenderServer() override;

	FRenderServer(const FRenderServer&) = delete;
	FRenderServer& operator=(const FRenderServer&) = delete;

	/** Start MahoRender + RHI worker, RHI (from Window), optional ImGui. */
	[[nodiscard]] bool Boot(FPlatformWindow& InWindow, const FConfig& Config);

	void TearDown();

	/**
	 * Game: wait MaxFramesInFlight, EndFrame/Capture ImGui, UpdatePlatformWindows (GLFW),
	 * enqueue ExecuteFrame on MahoRender. No Flush — MahoRHI owns ImGui viewport Vulkan.
	 */
	void Render(std::uint64_t FrameIndex);

	/** MahoRender: run stages + resource uploads + RHI Submit* for one packet. */
	void ExecuteFrame(FRenderFramePacket Packet);

	void WaitBeforeImGuiNewFrame(std::uint64_t FrameIndex);

	/** Current frame index on the render thread (for per-frame ring buffers). */
	[[nodiscard]] std::uint64_t GetCurrentFrameIndex() const { return CurrentFrameIndex; }

	void SetClearColor(float R, float G, float B, float A);

	/**
	 * Game thread: replace the pending scene snapshot consumed by the next Render().
	 * Value-copied into FRenderFramePacket — not Import/Export.
	 */
	void SubmitSceneUpdate(FSceneUpdatePacket Packet);

	/** Render thread: scene snapshot for the frame currently executing. */
	[[nodiscard]] const FSceneUpdatePacket& GetCurrentScene() const { return CurrentScene; }

	/**
	 * Client (Game): non-blocking resource → proxy upload.
	 * Requires TRenderResourceExporter<TResource> specialization (Texture/Mesh/Skeleton/Animation).
	 * Template bodies + explicit instantiations live in RenderServer.cpp.
	 */
	template <typename TResource>
	FTransferHandle QueueResourceUpload(const TResource& Resource);

	template <typename TResource>
	FTransferHandle RequestResourceDestroy(const TResource& Resource);

	[[nodiscard]] FTextureProxyRegistry& GetTextureProxyRegistry();
	[[nodiscard]] const FTextureProxyRegistry& GetTextureProxyRegistry() const;
	[[nodiscard]] FMeshProxyRegistry& GetMeshProxyRegistry();
	[[nodiscard]] const FMeshProxyRegistry& GetMeshProxyRegistry() const;
	[[nodiscard]] FSkeletonProxyRegistry& GetSkeletonProxyRegistry();
	[[nodiscard]] const FSkeletonProxyRegistry& GetSkeletonProxyRegistry() const;
	[[nodiscard]] FAnimationProxyRegistry& GetAnimationProxyRegistry();
	[[nodiscard]] const FAnimationProxyRegistry& GetAnimationProxyRegistry() const;

	[[nodiscard]] FImGuiSystem& GetImGui() { return ImGui; }
	[[nodiscard]] const FImGuiSystem& GetImGui() const { return ImGui; }

	[[nodiscard]] FRHIServer& GetRHIServer() { return RHIServer; }
	[[nodiscard]] const FRHIServer& GetRHIServer() const { return RHIServer; }

	[[nodiscard]] bool HasRHI() const { return RHIServer.HasRHI(); }
	[[nodiscard]] FVulkanRHI* GetVulkanRHI() const { return RHIServer.GetVulkanRHI(); }

	void SetImGuiEnabled(bool bEnabled) { bImGuiEnabled = bEnabled; }
	[[nodiscard]] bool IsImGuiEnabled() const { return bImGuiEnabled; }

	/** Game/editor view color target bound for ImGui::Image (set by render features). */
	void SetGameViewImGuiTexture(FImGuiTextureHandle Handle) { GameViewImGuiTexture = Handle; }
	[[nodiscard]] FImGuiTextureHandle GetGameViewImGuiTexture() const { return GameViewImGuiTexture; }
	[[nodiscard]] std::uint32_t GetGameViewWidth() const { return GameViewWidth; }
	[[nodiscard]] std::uint32_t GetGameViewHeight() const { return GameViewHeight; }
	void SetGameViewExtent(std::uint32_t Width, std::uint32_t Height)
	{
		GameViewWidth = Width;
		GameViewHeight = Height;
	}

	void RequestResize(int Width, int Height) { RHIServer.RequestResize(Width, Height); }

	[[nodiscard]] FPlatformWindow* GetBoundWindow() { return BoundWindow; }
	[[nodiscard]] const FPlatformWindow* GetBoundWindow() const { return BoundWindow; }

	template <typename T, typename... TArgs>
	T& RegisterFeature(TArgs&&... Args)
	{
		static_assert(std::is_base_of_v<IRenderFeature, T>, "T must derive from IRenderFeature");
		auto Feature = std::make_unique<T>(std::forward<TArgs>(Args)...);
		T& Ref = *Feature;

		if (HasRHI())
		{
			if (!Feature->OnRegister(*this))
			{
				MAHO_CORE_ERROR("FRenderServer: OnRegister failed for '{}'", Feature->GetName());
			}
		}
		else
		{
			MAHO_CORE_WARN("FRenderServer: RHI not ready when registering '{}' — OnRegister deferred to first frame", Feature->GetName());
		}

		Features.push_back(std::move(Feature));
		return Ref;
	}

	// Used by TRenderResourceExporter specializations (Game thread).
	void PushPendingTextureUpload(FTextureCpuSnapshot Snapshot, FTransferHandle Handle);
	void PushPendingTextureDestroy(std::string CatalogKey, FTransferHandle Handle);
	void PushPendingMeshUpload(FMeshCpuSnapshot Snapshot, FTransferHandle Handle);
	void PushPendingMeshDestroy(std::string CatalogKey, FTransferHandle Handle);
	void PushPendingSkeletonUpload(FSkeletonCpuSnapshot Snapshot, FTransferHandle Handle);
	void PushPendingSkeletonDestroy(std::string CatalogKey, FTransferHandle Handle);
	void PushPendingAnimationUpload(FAnimationCpuSnapshot Snapshot, FTransferHandle Handle);
	void PushPendingAnimationDestroy(std::string CatalogKey, FTransferHandle Handle);

protected:
	[[nodiscard]] const char* GetServerThreadName() const override { return "MahoRender"; }
	[[nodiscard]] const char* GetServerLogName() const override { return "RenderServer"; }

private:
	struct FPendingTextureUpload
	{
		FTextureCpuSnapshot Snapshot;
		FTransferHandle Handle;
	};
	struct FPendingMeshUpload
	{
		FMeshCpuSnapshot Snapshot;
		FTransferHandle Handle;
	};
	struct FPendingSkeletonUpload
	{
		FSkeletonCpuSnapshot Snapshot;
		FTransferHandle Handle;
	};
	struct FPendingAnimationUpload
	{
		FAnimationCpuSnapshot Snapshot;
		FTransferHandle Handle;
	};
	struct FPendingDestroy
	{
		std::string CatalogKey;
		FTransferHandle Handle;
	};

	void SyncFramebufferSize();
	[[nodiscard]] int GetMaxFramesInFlight() const;
	void BuildAndExecuteGraph(const FRenderFramePacket& Packet);
	void ProcessPendingResourceTransfers();
	[[nodiscard]] std::vector<IRenderFeature*> SortFeaturesForStage(
		std::vector<IRenderFeature*>& Participants,
		std::unordered_map<std::type_index, IRenderFeature*>& TypeMap,
		ERenderPipelineStage Stage);

	FRHIServer RHIServer;
	FPlatformWindow* BoundWindow = nullptr;
	FImGuiSystem ImGui;
	std::unique_ptr<FImGuiDrawDataRing> ImGuiDrawDataRing;
	std::unique_ptr<FTextureProxyRegistry> TextureProxies;
	std::unique_ptr<FMeshProxyRegistry> MeshProxies;
	std::unique_ptr<FSkeletonProxyRegistry> SkeletonProxies;
	std::unique_ptr<FAnimationProxyRegistry> AnimationProxies;
	std::vector<std::unique_ptr<IRenderFeature>> Features;

	std::mutex PendingUploadMutex;
	std::vector<FPendingTextureUpload> PendingTextureUploads;
	std::vector<FPendingDestroy> PendingTextureDestroys;
	std::vector<FPendingMeshUpload> PendingMeshUploads;
	std::vector<FPendingDestroy> PendingMeshDestroys;
	std::vector<FPendingSkeletonUpload> PendingSkeletonUploads;
	std::vector<FPendingDestroy> PendingSkeletonDestroys;
	std::vector<FPendingAnimationUpload> PendingAnimationUploads;
	std::vector<FPendingDestroy> PendingAnimationDestroys;

	std::uint64_t CurrentFrameIndex = 0;
	FSceneUpdatePacket CurrentScene;
	FSceneUpdatePacket PendingScene;

	float ClearColorR = 0.08f;
	float ClearColorG = 0.10f;
	float ClearColorB = 0.16f;
	float ClearColorA = 1.0f;

	bool bImGuiEnabled = false;
	FImGuiTextureHandle GameViewImGuiTexture;
	std::uint32_t GameViewWidth = 0;
	std::uint32_t GameViewHeight = 0;
	int LastFramebufferWidth = 0;
	int LastFramebufferHeight = 0;
};

} // namespace Maho
