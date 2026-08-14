#pragma once

#include <Core/Engine/Engine.h>
#include <Core/Misc/Export.h>
#include <Core/Misc/DependsPack.h>
#include <Core/Engine/EngineExtension.h>
#include <Core/Misc/TypeList.h>
#include <Core/Extension/Platform/Platform.h>
#include <Core/Server/ThreadedServer.h>
#include <Core/Server/TransferHandle.h>
#include <Core/Extension/Platform/PlatformWindow.h>
#include <Render/RenderFramePacket.h>
#include <Render/RHI/RHIServer.h>
#include <Render/RDG/RDGBuilder.h>
#include <Render/RenderFeature.h>
#include <Render/RenderPipelineStage.h>
#include <Render/UI/ImGuiSystem.h>

#include <cstdint>
#include <memory>
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

/**
 * Render extension + render thread worker.
 *
 * Game thread interacts with this class through the IEngineExtension stage
 * interface (Init / Shutdown) plus the FGameApp-driven BeginFrame / RenderFrame
 * frame methods. It also owns the MahoRender worker thread (FThreadedServer)
 * which builds and executes the per-frame FRDGBuilder graph, dispatching
 * registered IRenderFeatures.
 */
class MAHO_API FRenderSystem final
	: public IEngineExtension
	, public TDependsPack<
		TDependsOn<EEngineStage::Init, TTypeList<FPlatformSystem>>>
	, public FThreadedServer
{
public:
	static constexpr int MaxFramesInFlightCap = 3;

	FRenderSystem();
	~FRenderSystem() override;

	FRenderSystem(const FRenderSystem&) = delete;
	FRenderSystem& operator=(const FRenderSystem&) = delete;

	// ── IEngineExtension ──
	[[nodiscard]] const char* GetName() const override { return "Render"; }
	bool ExecuteStage(EEngineStage Stage) override;

	// ── Render world frame (driven by FGameApp) ──
	void BeginFrame();
	void RenderFrame();

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
	 * Game thread: gather every registered feature's context slice from the
	 * ECS world into an FGameFrameContext, consumed by the next Render().
	 */
	[[nodiscard]] FGameFrameContext GatherContexts(FWorld& World);

	/** Game thread: replace the pending frame context consumed by the next Render(). */
	void SubmitFrameContext(FGameFrameContext FrameContext);

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
				MAHO_CORE_ERROR("FRenderSystem: OnRegister failed for '{}'", Feature->GetName());
			}
		}
		else
		{
							MAHO_CORE_WARN("FRenderSystem: RHI not ready when registering '{}' — OnRegister deferred to first frame", Feature->GetName());
		}

		Features.push_back(std::move(Feature));
		return Ref;
	}

protected:
	[[nodiscard]] const char* GetServerThreadName() const override { return "MahoRender"; }
	[[nodiscard]] const char* GetServerLogName() const override { return "RenderServer"; }

private:
	void SyncFramebufferSize();
	[[nodiscard]] int GetMaxFramesInFlight() const;
	void BuildAndExecuteGraph(const FRenderFramePacket& Packet);
	[[nodiscard]] std::vector<IRenderFeature*> SortFeaturesForStage(
		std::vector<IRenderFeature*>& Participants,
		std::unordered_map<std::type_index, IRenderFeature*>& TypeMap,
		ERenderPipelineStage Stage);

	FRHIServer RHIServer;
	FPlatformWindow* BoundWindow = nullptr;
	FImGuiSystem ImGui;
	std::unique_ptr<FImGuiDrawDataRing> ImGuiDrawDataRing;
	std::vector<std::unique_ptr<IRenderFeature>> Features;

	std::uint64_t CurrentFrameIndex = 0;
	FGameFrameContext PendingFrameContext;
	FFrameContext FrameContexts[3];

	/** Per-frame render graph — reset and reused every frame by all features. */
	FRDGBuilder FrameGraph;

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
