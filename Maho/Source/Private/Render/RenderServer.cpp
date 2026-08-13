#include <Render/RenderServer.h>
#include <Render/RenderCommand.h>
#include <Render/RDG/RDGBuilder.h>

#include <Core/Application/App.h>
#include <Core/Extension/Render/Render.h>
#include <Core/System/Console.h>
#include <Core/System/Log.h>
#include "Render/AnimationRenderProxy.h"
#include "Render/MeshRenderProxy.h"
#include "Render/SkeletonRenderProxy.h"
#include "Render/TextureRenderProxy.h"
#include "Render/UI/ImGuiDrawDataRing.h"

#if defined(MAHO_WITH_IMGUI)
#	include <imgui.h>
#endif

#include <algorithm>
#include <utility>

namespace Maho
{

namespace
{

static TAutoConsoleVariable GCVarMaxFramesInFlight(
	"r.MaxFramesInFlight",
	3,
	"Max Game frames submitted to RHI before waiting (1..3)");

static_assert(
	FRenderServer::MaxFramesInFlightCap == ImGuiDrawDataRingSlotCount,
	"ImGui draw-data ring slot count must match MaxFramesInFlightCap");

} // namespace

namespace Detail
{

FRenderServer* GetRenderServer()
{
	if (!GApp)
	{
		return nullptr;
	}
	FRenderSystem* System = GApp->GetExtension<FRenderSystem>();
	return System ? &System->GetRenderServer() : nullptr;
}

} // namespace Detail

FRenderServer::FRenderServer()
	: ImGuiDrawDataRing(std::make_unique<FImGuiDrawDataRing>())
	, TextureProxies(std::make_unique<FTextureProxyRegistry>())
	, MeshProxies(std::make_unique<FMeshProxyRegistry>())
	, SkeletonProxies(std::make_unique<FSkeletonProxyRegistry>())
	, AnimationProxies(std::make_unique<FAnimationProxyRegistry>())
{
}

FRenderServer::~FRenderServer()
{
	TearDown();
}

int FRenderServer::GetMaxFramesInFlight() const
{
	const int Requested = GCVarMaxFramesInFlight.GetValue();
	return (std::clamp)(Requested, 1, MaxFramesInFlightCap);
}

void FRenderServer::SetClearColor(float R, float G, float B, float A)
{
	ClearColorR = R;
	ClearColorG = G;
	ClearColorB = B;
	ClearColorA = A;
}

void FRenderServer::SubmitSceneUpdate(FSceneUpdatePacket Packet)
{
	PendingScene = std::move(Packet);
}

FTextureProxyRegistry& FRenderServer::GetTextureProxyRegistry()
{
	return *TextureProxies;
}

const FTextureProxyRegistry& FRenderServer::GetTextureProxyRegistry() const
{
	return *TextureProxies;
}

FMeshProxyRegistry& FRenderServer::GetMeshProxyRegistry()
{
	return *MeshProxies;
}

const FMeshProxyRegistry& FRenderServer::GetMeshProxyRegistry() const
{
	return *MeshProxies;
}

FSkeletonProxyRegistry& FRenderServer::GetSkeletonProxyRegistry()
{
	return *SkeletonProxies;
}

const FSkeletonProxyRegistry& FRenderServer::GetSkeletonProxyRegistry() const
{
	return *SkeletonProxies;
}

FAnimationProxyRegistry& FRenderServer::GetAnimationProxyRegistry()
{
	return *AnimationProxies;
}

const FAnimationProxyRegistry& FRenderServer::GetAnimationProxyRegistry() const
{
	return *AnimationProxies;
}

void FRenderServer::PushPendingTextureUpload(FTextureCpuSnapshot Snapshot, FTransferHandle Handle)
{
	std::lock_guard<std::mutex> Lock(PendingUploadMutex);
	PendingTextureUploads.push_back(FPendingTextureUpload{ std::move(Snapshot), Handle });
}

void FRenderServer::PushPendingTextureDestroy(std::string CatalogKey, FTransferHandle Handle)
{
	std::lock_guard<std::mutex> Lock(PendingUploadMutex);
	PendingTextureDestroys.push_back(FPendingDestroy{ std::move(CatalogKey), Handle });
}

void FRenderServer::PushPendingMeshUpload(FMeshCpuSnapshot Snapshot, FTransferHandle Handle)
{
	std::lock_guard<std::mutex> Lock(PendingUploadMutex);
	PendingMeshUploads.push_back(FPendingMeshUpload{ std::move(Snapshot), Handle });
}

void FRenderServer::PushPendingMeshDestroy(std::string CatalogKey, FTransferHandle Handle)
{
	std::lock_guard<std::mutex> Lock(PendingUploadMutex);
	PendingMeshDestroys.push_back(FPendingDestroy{ std::move(CatalogKey), Handle });
}

void FRenderServer::PushPendingSkeletonUpload(FSkeletonCpuSnapshot Snapshot, FTransferHandle Handle)
{
	std::lock_guard<std::mutex> Lock(PendingUploadMutex);
	PendingSkeletonUploads.push_back(FPendingSkeletonUpload{ std::move(Snapshot), Handle });
}

void FRenderServer::PushPendingSkeletonDestroy(std::string CatalogKey, FTransferHandle Handle)
{
	std::lock_guard<std::mutex> Lock(PendingUploadMutex);
	PendingSkeletonDestroys.push_back(FPendingDestroy{ std::move(CatalogKey), Handle });
}

void FRenderServer::PushPendingAnimationUpload(FAnimationCpuSnapshot Snapshot, FTransferHandle Handle)
{
	std::lock_guard<std::mutex> Lock(PendingUploadMutex);
	PendingAnimationUploads.push_back(FPendingAnimationUpload{ std::move(Snapshot), Handle });
}

void FRenderServer::PushPendingAnimationDestroy(std::string CatalogKey, FTransferHandle Handle)
{
	std::lock_guard<std::mutex> Lock(PendingUploadMutex);
	PendingAnimationDestroys.push_back(FPendingDestroy{ std::move(CatalogKey), Handle });
}

bool FRenderServer::Boot(FPlatformWindow& InWindow, const FConfig& Config)
{
	BoundWindow = &InWindow;
	ClearColorR = Config.ClearColorR;
	ClearColorG = Config.ClearColorG;
	ClearColorB = Config.ClearColorB;
	ClearColorA = Config.ClearColorA;

	if (!Initialize())
	{
		MAHO_CORE_ERROR("RenderServer: MahoRender Initialize failed");
		BoundWindow = nullptr;
		return false;
	}

	if (!RHIServer.Initialize())
	{
		MAHO_CORE_ERROR("RenderServer: RHIServer Initialize failed");
		Shutdown();
		BoundWindow = nullptr;
		return false;
	}

	if (!RHIServer.InitializeRHI(InWindow))
	{
		MAHO_CORE_ERROR("RenderServer: InitializeRHI failed");
		RHIServer.Shutdown();
		Shutdown();
		BoundWindow = nullptr;
		return false;
	}

	TextureProxies->EnsureDefaultPlaceholder(RHIServer);
	MeshProxies->EnsureDefaultPlaceholder(RHIServer);
	SkeletonProxies->EnsureDefaultPlaceholder(RHIServer);
	AnimationProxies->EnsureDefaultPlaceholder();

	if (InWindow.HasOsWindow())
	{
		InWindow.GetFramebufferSize(LastFramebufferWidth, LastFramebufferHeight);
		if (!ImGui.Initialize(InWindow, RHIServer, Config.ProjectConfigDir))
		{
			MAHO_CORE_ERROR("RenderServer: ImGui Initialize failed");
			RHIServer.Shutdown();
			Shutdown();
			BoundWindow = nullptr;
			return false;
		}
		SetImGuiEnabled(true);
	}

	RHIServer.ResetFrameFence();
	CurrentFrameIndex = 0;

	MAHO_CORE_INFO("RenderServer: Boot ok (MaxFramesInFlight={})", GetMaxFramesInFlight());
	return true;
}

void FRenderServer::TearDown()
{
	if (IsInitialized())
	{
		Flush();
	}

	// Unregister features while RHI is still alive (GPU allocs / ImGui external textures).
	if (RHIServer.IsInitialized())
	{
		RHIServer.Flush();
	}
	for (auto It = Features.rbegin(); It != Features.rend(); ++It)
	{
		if (*It)
		{
			(*It)->OnUnregister(*this);
		}
	}
	Features.clear();
	GameViewImGuiTexture.Reset();
	GameViewWidth = 0;
	GameViewHeight = 0;

	if (AnimationProxies)
	{
		AnimationProxies->DestroyAll(RHIServer);
	}
	if (SkeletonProxies && RHIServer.IsInitialized())
	{
		SkeletonProxies->DestroyAll(RHIServer);
	}
	if (MeshProxies && RHIServer.IsInitialized())
	{
		MeshProxies->DestroyAll(RHIServer);
	}
	if (TextureProxies && RHIServer.IsInitialized())
	{
		TextureProxies->DestroyAll(RHIServer);
	}

	if (RHIServer.IsInitialized())
	{
		RHIServer.Flush();
	}

	SetImGuiEnabled(false);
	if (ImGui.IsInitialized())
	{
		ImGui.Shutdown(RHIServer);
	}
	if (RHIServer.IsInitialized())
	{
		RHIServer.Shutdown();
	}
	if (ImGuiDrawDataRing)
	{
		ImGuiDrawDataRing->ReleaseAll();
	}

	{
		std::lock_guard<std::mutex> Lock(PendingUploadMutex);
		PendingTextureUploads.clear();
		PendingTextureDestroys.clear();
		PendingMeshUploads.clear();
		PendingMeshDestroys.clear();
		PendingSkeletonUploads.clear();
		PendingSkeletonDestroys.clear();
		PendingAnimationUploads.clear();
		PendingAnimationDestroys.clear();
	}

	if (IsInitialized())
	{
		Shutdown();
	}

	BoundWindow = nullptr;
	LastFramebufferWidth = 0;
	LastFramebufferHeight = 0;
	CurrentFrameIndex = 0;
}

void FRenderServer::WaitBeforeImGuiNewFrame(std::uint64_t FrameIndex)
{
	if (FrameIndex > 1)
	{
		RHIServer.WaitForRenderFrame(FrameIndex - 1);
	}
}

std::vector<IRenderFeature*> FRenderServer::SortFeaturesForStage(
	std::vector<IRenderFeature*>& Participants,
	std::unordered_map<std::type_index, IRenderFeature*>& TypeMap,
	ERenderPipelineStage Stage)
{
	std::unordered_map<IRenderFeature*, std::vector<IRenderFeature*>> Deps;
	std::unordered_map<IRenderFeature*, int32_t> InDegree;

	for (auto* F : Participants)
	{
		InDegree.try_emplace(F, 0);
		F->ForEachStageDep(Stage, [&](const std::type_index& DepType) {
			auto It = TypeMap.find(DepType);
			if (It != TypeMap.end() && It->second->ParticipatesInStage(Stage))
			{
				Deps[It->second].push_back(F);
				InDegree[F]++;
			}
		});
	}

	// Kahn topological sort
	std::vector<IRenderFeature*> Sorted;
	std::queue<IRenderFeature*> Q;

	for (auto* F : Participants)
	{
		if (InDegree[F] == 0)
		{
			Q.push(F);
		}
	}

	while (!Q.empty())
	{
		auto* F = Q.front();
		Q.pop();
		Sorted.push_back(F);
		for (auto* Next : Deps[F])
		{
			if (--InDegree[Next] == 0)
			{
				Q.push(Next);
			}
		}
	}

	if (Sorted.size() != Participants.size())
	{
		MAHO_CORE_WARN("FRenderServer: circular feature dependency in stage, falling back to declaration order");
		return Participants;
	}

	return Sorted;
}

void FRenderServer::BuildAndExecuteGraph(const FRenderFramePacket& Packet)
{
	static constexpr ERenderPipelineStage PipelineStages[] =
	{
		ERenderPipelineStage::BeginFrame,
		ERenderPipelineStage::DepthPrePass,
		ERenderPipelineStage::ShadowMap,
		ERenderPipelineStage::BasePass,
		ERenderPipelineStage::Translucent,
		ERenderPipelineStage::PostProcess,
		ERenderPipelineStage::EndFrame,
	};

	// Build type -> feature map
	std::unordered_map<std::type_index, IRenderFeature*> TypeMap;
	for (auto& F : Features)
	{
		TypeMap[std::type_index(typeid(*F))] = F.get();
	}

	for (ERenderPipelineStage Stage : PipelineStages)
	{
		// Collect participants for this stage
		std::vector<IRenderFeature*> Participants;
		for (auto& F : Features)
		{
			if (F->ParticipatesInStage(Stage))
			{
				Participants.push_back(F.get());
			}
		}

		if (Participants.empty())
		{
			continue;
		}

		// Sort by dependency graph
		std::vector<IRenderFeature*> Sorted = SortFeaturesForStage(Participants, TypeMap, Stage);

		// Build render graph for this stage
		FRDGBuilder GraphBuilder(RHIServer.GetRHI());
		bool bHasPasses = false;

		for (auto* Feature : Sorted)
		{
			std::size_t PassCountBefore = GraphBuilder.GetPassCount();
			Feature->ExecuteStage(Stage, GraphBuilder);
			if (GraphBuilder.GetPassCount() > PassCountBefore)
			{
				bHasPasses = true;
			}
		}

		if (bHasPasses)
		{
			GraphBuilder.Compile();
			GraphBuilder.Execute();
		}
	}

	// Built-in: swapchain present + ImGui
	RHIServer.SubmitBeginMainPass(
		Packet.ClearColorR,
		Packet.ClearColorG,
		Packet.ClearColorB,
		Packet.ClearColorA);
	if (Packet.bSubmitImGui && Packet.ImGuiSlotIndex >= 0 && ImGuiDrawDataRing)
	{
		RHIServer.SubmitRenderUI(*ImGuiDrawDataRing, Packet.ImGuiSlotIndex);
	}
	if (Packet.bSubmitImGuiViewports)
	{
		RHIServer.SubmitRenderPlatformWindows();
	}
	RHIServer.SubmitEndFrameAndFence(Packet.FrameIndex);
}

void FRenderServer::ProcessPendingResourceTransfers()
{
	std::vector<FPendingTextureUpload> TextureUploads;
	std::vector<FPendingDestroy> TextureDestroys;
	std::vector<FPendingMeshUpload> MeshUploads;
	std::vector<FPendingDestroy> MeshDestroys;
	std::vector<FPendingSkeletonUpload> SkeletonUploads;
	std::vector<FPendingDestroy> SkeletonDestroys;
	std::vector<FPendingAnimationUpload> AnimationUploads;
	std::vector<FPendingDestroy> AnimationDestroys;
	{
		std::lock_guard<std::mutex> Lock(PendingUploadMutex);
		TextureUploads.swap(PendingTextureUploads);
		TextureDestroys.swap(PendingTextureDestroys);
		MeshUploads.swap(PendingMeshUploads);
		MeshDestroys.swap(PendingMeshDestroys);
		SkeletonUploads.swap(PendingSkeletonUploads);
		SkeletonDestroys.swap(PendingSkeletonDestroys);
		AnimationUploads.swap(PendingAnimationUploads);
		AnimationDestroys.swap(PendingAnimationDestroys);
	}

	for (FPendingDestroy& Item : TextureDestroys)
	{
		TextureProxies->Destroy(RHIServer, Item.CatalogKey, Item.Handle);
	}
	for (FPendingTextureUpload& Item : TextureUploads)
	{
		TextureProxies->BeginUpload(RHIServer, std::move(Item.Snapshot), Item.Handle);
	}
	TextureProxies->PollInFlight(RHIServer);

	for (FPendingDestroy& Item : MeshDestroys)
	{
		MeshProxies->Destroy(RHIServer, Item.CatalogKey, Item.Handle);
	}
	for (FPendingMeshUpload& Item : MeshUploads)
	{
		MeshProxies->BeginUpload(RHIServer, std::move(Item.Snapshot), Item.Handle);
	}
	MeshProxies->PollInFlight(RHIServer);

	for (FPendingDestroy& Item : SkeletonDestroys)
	{
		SkeletonProxies->Destroy(RHIServer, Item.CatalogKey, Item.Handle);
	}
	for (FPendingSkeletonUpload& Item : SkeletonUploads)
	{
		SkeletonProxies->BeginUpload(RHIServer, std::move(Item.Snapshot), Item.Handle);
	}
	SkeletonProxies->PollInFlight(RHIServer);

	for (FPendingDestroy& Item : AnimationDestroys)
	{
		AnimationProxies->Destroy(RHIServer, Item.CatalogKey, Item.Handle);
	}
	for (FPendingAnimationUpload& Item : AnimationUploads)
	{
		AnimationProxies->BeginUpload(RHIServer, std::move(Item.Snapshot), Item.Handle);
	}
}

void FRenderServer::ExecuteFrame(FRenderFramePacket Packet)
{
	CurrentFrameIndex = Packet.FrameIndex;
	CurrentScene = std::move(Packet.Scene);

	if (Packet.bResizeFramebuffer && Packet.FramebufferWidth > 0 && Packet.FramebufferHeight > 0)
	{
		LastFramebufferWidth = Packet.FramebufferWidth;
		LastFramebufferHeight = Packet.FramebufferHeight;
		RHIServer.RequestResize(Packet.FramebufferWidth, Packet.FramebufferHeight);
	}

	ProcessPendingResourceTransfers();
	BuildAndExecuteGraph(Packet);
}

void FRenderServer::Render(std::uint64_t FrameIndex)
{
	const int MaxInFlight = GetMaxFramesInFlight();
	if (FrameIndex > static_cast<std::uint64_t>(MaxInFlight))
	{
		RHIServer.WaitForRenderFrame(FrameIndex - static_cast<std::uint64_t>(MaxInFlight));
	}

	FRenderFramePacket Packet{};
	Packet.FrameIndex = FrameIndex;
	Packet.ClearColorR = ClearColorR;
	Packet.ClearColorG = ClearColorG;
	Packet.ClearColorB = ClearColorB;
	Packet.ClearColorA = ClearColorA;
	Packet.Scene = std::move(PendingScene);
	PendingScene = FSceneUpdatePacket{};

	int Width = LastFramebufferWidth;
	int Height = LastFramebufferHeight;
	if (BoundWindow && BoundWindow->HasOsWindow() && HasRHI())
	{
		BoundWindow->GetFramebufferSize(Width, Height);
		if (Width > 0 && Height > 0
			&& (Width != LastFramebufferWidth || Height != LastFramebufferHeight))
		{
			Packet.bResizeFramebuffer = true;
			Packet.FramebufferWidth = Width;
			Packet.FramebufferHeight = Height;
		}
	}

	Packet.ImGuiSlotIndex = -1;
	Packet.bSubmitImGui = false;
	Packet.bSubmitImGuiViewports = false;
	if (ImGui.IsInitialized())
	{
#if defined(MAHO_WITH_IMGUI)
		// Composite the game view fullscreen when a render feature registered one.
		// Editor (when present) overlays its DockSpace panels on top of this background.
		if (GameViewImGuiTexture.IsValid() && GameViewWidth > 0 && GameViewHeight > 0)
		{
			ImGuiViewport* Viewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(Viewport->Pos);
			ImGui::SetNextWindowSize(Viewport->Size);
			ImGui::Begin("##GameView", nullptr,
				ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
				ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground |
				ImGuiWindowFlags_NoSavedSettings);
			ImGui::Image(reinterpret_cast<ImTextureID>(GameViewImGuiTexture.Id), Viewport->Size);
			ImGui::End();
		}
#endif
		ImGui.EndFrame();
		if (ImGuiDrawDataRing)
		{
			Packet.ImGuiSlotIndex = ImGuiDrawDataRing->CaptureFromImGui(FrameIndex);
			Packet.bSubmitImGui = bImGuiEnabled && Packet.ImGuiSlotIndex >= 0;
		}
		Packet.bSubmitImGuiViewports = bImGuiEnabled;
		// GLFW platform windows on game thread; Vulkan viewport submit is on MahoRHI.
		ImGui.UpdatePlatformWindows();
	}

	ENQUEUE_RENDER_COMMAND(RenderFrame)(
		[Packet = std::move(Packet)](FRenderServer& Server) mutable
		{
			Server.ExecuteFrame(std::move(Packet));
		});
}

void FRenderServer::SyncFramebufferSize()
{
	// Framebuffer sync is performed on Game in Render() into FRenderFramePacket.
}

} // namespace Maho
