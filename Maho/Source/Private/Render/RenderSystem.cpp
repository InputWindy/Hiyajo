#include <Render/RenderSystem.h>
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
	FRenderSystem::MaxFramesInFlightCap == ImGuiDrawDataRingSlotCount,
	"ImGui draw-data ring slot count must match MaxFramesInFlightCap");

} // namespace

namespace Detail
{

FRenderSystem* GetRenderSystem()
{
	return GApp ? GApp->GetExtension<FRenderSystem>() : nullptr;
}

} // namespace Detail

bool FRenderSystem::ExecuteStage(EEngineStage Stage)
{
	switch (Stage)
	{
	case EEngineStage::Init:
	{
		if (!GApp)
		{
			MAHO_CORE_ERROR("FRenderSystem: GApp missing at Init");
			return false;
		}
		FPlatformSystem* Platform = GApp->GetExtension<FPlatformSystem>();
		FPlatformWindow* Window = Platform ? Platform->GetWindow() : nullptr;
		if (!Window)
		{
			MAHO_CORE_ERROR("FRenderSystem: no platform window at Init");
			return false;
		}
		if (!Boot(*Window, GApp->GetConfig()))
		{
			MAHO_CORE_ERROR("FRenderSystem: RenderServer.Boot failed");
			return false;
		}
		return true;
	}
	case EEngineStage::BeginFrame:
		if (GApp && GetImGui().IsInitialized())
		{
			WaitBeforeImGuiNewFrame(GApp->GetFrameIndex());
			GetImGui().BeginFrame();
		}
		return true;
	case EEngineStage::Render:
		if (GApp)
		{
			Render(GApp->GetFrameIndex());
		}
		return true;
	case EEngineStage::Shutdown:
		TearDown();
		return true;
	default:
		return true;
	}
}

FRenderSystem::FRenderSystem()
	: ImGuiDrawDataRing(std::make_unique<FImGuiDrawDataRing>())
	, TextureProxies(std::make_unique<FTextureProxyRegistry>())
	, MeshProxies(std::make_unique<FMeshProxyRegistry>())
	, SkeletonProxies(std::make_unique<FSkeletonProxyRegistry>())
	, AnimationProxies(std::make_unique<FAnimationProxyRegistry>())
{
}

FRenderSystem::~FRenderSystem()
{
	TearDown();
}

int FRenderSystem::GetMaxFramesInFlight() const
{
	const int Requested = GCVarMaxFramesInFlight.GetValue();
	return (std::clamp)(Requested, 1, MaxFramesInFlightCap);
}

void FRenderSystem::SetClearColor(float R, float G, float B, float A)
{
	ClearColorR = R;
	ClearColorG = G;
	ClearColorB = B;
	ClearColorA = A;
}

FGameFrameContext FRenderSystem::GatherContexts(FWorld& World)
{
	FGameFrameContext Frame;
	Frame.Slices.reserve(Features.size());
	for (auto& F : Features)
	{
		Frame.Slices.push_back(F->GatherContext(World));
	}
	return Frame;
}

void FRenderSystem::SubmitFrameContext(FGameFrameContext FrameContext)
{
	PendingFrameContext = std::move(FrameContext);
}

FTextureProxyRegistry& FRenderSystem::GetTextureProxyRegistry()
{
	return *TextureProxies;
}

const FTextureProxyRegistry& FRenderSystem::GetTextureProxyRegistry() const
{
	return *TextureProxies;
}

FMeshProxyRegistry& FRenderSystem::GetMeshProxyRegistry()
{
	return *MeshProxies;
}

const FMeshProxyRegistry& FRenderSystem::GetMeshProxyRegistry() const
{
	return *MeshProxies;
}

FSkeletonProxyRegistry& FRenderSystem::GetSkeletonProxyRegistry()
{
	return *SkeletonProxies;
}

const FSkeletonProxyRegistry& FRenderSystem::GetSkeletonProxyRegistry() const
{
	return *SkeletonProxies;
}

FAnimationProxyRegistry& FRenderSystem::GetAnimationProxyRegistry()
{
	return *AnimationProxies;
}

const FAnimationProxyRegistry& FRenderSystem::GetAnimationProxyRegistry() const
{
	return *AnimationProxies;
}

void FRenderSystem::PushPendingTextureUpload(FTextureCpuSnapshot Snapshot, FTransferHandle Handle)
{
	std::lock_guard<std::mutex> Lock(PendingUploadMutex);
	PendingTextureUploads.push_back(FPendingTextureUpload{ std::move(Snapshot), Handle });
}

void FRenderSystem::PushPendingTextureDestroy(std::string CatalogKey, FTransferHandle Handle)
{
	std::lock_guard<std::mutex> Lock(PendingUploadMutex);
	PendingTextureDestroys.push_back(FPendingDestroy{ std::move(CatalogKey), Handle });
}

void FRenderSystem::PushPendingMeshUpload(FMeshCpuSnapshot Snapshot, FTransferHandle Handle)
{
	std::lock_guard<std::mutex> Lock(PendingUploadMutex);
	PendingMeshUploads.push_back(FPendingMeshUpload{ std::move(Snapshot), Handle });
}

void FRenderSystem::PushPendingMeshDestroy(std::string CatalogKey, FTransferHandle Handle)
{
	std::lock_guard<std::mutex> Lock(PendingUploadMutex);
	PendingMeshDestroys.push_back(FPendingDestroy{ std::move(CatalogKey), Handle });
}

void FRenderSystem::PushPendingSkeletonUpload(FSkeletonCpuSnapshot Snapshot, FTransferHandle Handle)
{
	std::lock_guard<std::mutex> Lock(PendingUploadMutex);
	PendingSkeletonUploads.push_back(FPendingSkeletonUpload{ std::move(Snapshot), Handle });
}

void FRenderSystem::PushPendingSkeletonDestroy(std::string CatalogKey, FTransferHandle Handle)
{
	std::lock_guard<std::mutex> Lock(PendingUploadMutex);
	PendingSkeletonDestroys.push_back(FPendingDestroy{ std::move(CatalogKey), Handle });
}

void FRenderSystem::PushPendingAnimationUpload(FAnimationCpuSnapshot Snapshot, FTransferHandle Handle)
{
	std::lock_guard<std::mutex> Lock(PendingUploadMutex);
	PendingAnimationUploads.push_back(FPendingAnimationUpload{ std::move(Snapshot), Handle });
}

void FRenderSystem::PushPendingAnimationDestroy(std::string CatalogKey, FTransferHandle Handle)
{
	std::lock_guard<std::mutex> Lock(PendingUploadMutex);
	PendingAnimationDestroys.push_back(FPendingDestroy{ std::move(CatalogKey), Handle });
}

bool FRenderSystem::Boot(FPlatformWindow& InWindow, const FConfig& Config)
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

void FRenderSystem::TearDown()
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

void FRenderSystem::WaitBeforeImGuiNewFrame(std::uint64_t FrameIndex)
{
	if (FrameIndex > 1)
	{
		RHIServer.WaitForRenderFrame(FrameIndex - 1);
	}
}

std::vector<IRenderFeature*> FRenderSystem::SortFeaturesForStage(
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
		MAHO_CORE_WARN("FRenderSystem: circular feature dependency in stage, falling back to declaration order");
		return Participants;
	}

	return Sorted;
}

void FRenderSystem::BuildAndExecuteGraph(const FRenderFramePacket& Packet)
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

	FFrameContext& FrameCtx = FrameContexts[static_cast<std::size_t>(Packet.FrameIndex % 3)];
	FrameCtx.FrameIndex = Packet.FrameIndex;

	// Build type -> feature map + feature -> slice map (gather order matches Features order).
	std::unordered_map<std::type_index, IRenderFeature*> TypeMap;
	std::unordered_map<IRenderFeature*, const IGameContextSlice*> SliceMap;
	for (std::size_t I = 0; I < Features.size(); ++I)
	{
		TypeMap[std::type_index(typeid(*Features[I]))] = Features[I].get();
		if (I < Packet.FrameContext.Slices.size())
		{
			SliceMap[Features[I].get()] = Packet.FrameContext.Slices[I].get();
		}
	}

	// Build one graph for the whole frame — all features across all stages
	// declare passes into it, so the RDG can sort/optimize globally.
	FrameGraph.Reset(&RHIServer);

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

		for (auto* Feature : Sorted)
		{
			const IGameContextSlice* Slice = nullptr;
			auto SliceIt = SliceMap.find(Feature);
			if (SliceIt != SliceMap.end())
			{
				Slice = SliceIt->second;
			}

			if (Slice)
			{
				Feature->ExecuteStage(Stage, *Slice, FrameCtx, FrameGraph);
			}
		}
	}

	// Frame finalize: ImGui composite + swapchain present, expressed as the
	// graph's terminal step. The internal multi-submit stays hidden behind
	// FrameGraph.Execute().
	FrameGraph.SetFrameFinalize([this,
		ClearR = Packet.ClearColorR, ClearG = Packet.ClearColorG,
		ClearB = Packet.ClearColorB, ClearA = Packet.ClearColorA,
		ImGuiSlot = Packet.ImGuiSlotIndex, bSubmitImGui = Packet.bSubmitImGui,
		bSubmitViewports = Packet.bSubmitImGuiViewports,
		FrameIndex = Packet.FrameIndex]()
	{
		RHIServer.SubmitBeginMainPass(ClearR, ClearG, ClearB, ClearA);
		if (bSubmitImGui && ImGuiSlot >= 0 && ImGuiDrawDataRing)
		{
			RHIServer.SubmitRenderUI(*ImGuiDrawDataRing, ImGuiSlot);
		}
		if (bSubmitViewports)
		{
			RHIServer.SubmitRenderPlatformWindows();
		}
		RHIServer.SubmitEndFrameAndFence(FrameIndex);
	});

	FrameGraph.Compile();
	FrameGraph.Execute();
}

void FRenderSystem::ProcessPendingResourceTransfers()
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

void FRenderSystem::ExecuteFrame(FRenderFramePacket Packet)
{
	CurrentFrameIndex = Packet.FrameIndex;

	if (Packet.bResizeFramebuffer && Packet.FramebufferWidth > 0 && Packet.FramebufferHeight > 0)
	{
		LastFramebufferWidth = Packet.FramebufferWidth;
		LastFramebufferHeight = Packet.FramebufferHeight;
		RHIServer.RequestResize(Packet.FramebufferWidth, Packet.FramebufferHeight);
	}

	ProcessPendingResourceTransfers();
	BuildAndExecuteGraph(Packet);
}

void FRenderSystem::Render(std::uint64_t FrameIndex)
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
	Packet.FrameContext = std::move(PendingFrameContext);
	PendingFrameContext = FGameFrameContext{};

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
		[Packet = std::move(Packet)](FRenderSystem& Server) mutable
		{
			Server.ExecuteFrame(std::move(Packet));
		});
}

void FRenderSystem::SyncFramebufferSize()
{
	// Framebuffer sync is performed on Game in Render() into FRenderFramePacket.
}

} // namespace Maho
