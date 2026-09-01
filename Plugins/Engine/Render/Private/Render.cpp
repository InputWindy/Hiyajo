#include "Render.h"

#include <DrawTriangleFeature.h>
#include <Frame.h>
#include <ImGuiRender.h>
#include <ImGuiSystem.h>
#include <Log.h>
#include <Platform.h>
#include <Scene.h>
#include "RenderResourcePool.h"

// Unity build: fold the shader compiler + resource pool TUs in (the codegen
// target only compiles Render.cpp; the RHI plugin uses the same pattern).
#include "ShaderCompiler.cpp"
#include "RenderResourcePool.cpp"

namespace Maho
{

FRender* GRender = nullptr;
MAHO_RENDER_API FRender* GetRender()
{
	return GRender;
}

FRender::FRender()
{
	// Init: I read the window from Platform (its PostInitialize) and log heavily
	// (Log must be up first) -- both declared by ME, the consumer.
	WaitFor<IInit, Platform::FPlatform, IPostInit>();
	WaitFor<IInit, FLog, IInit>();

	// Shutdown: my teardown drains render tasks that may log, and I hold the RHI
	// surface created from Platform's window -- so Log and Platform must run
	// their Shutdown AFTER mine. Declared here (I know them), not by them.
	BlockOn<FLog, IShutdown, IShutdown>();
	BlockOn<Platform::FPlatform, IShutdown, IShutdown>();

	// Input: Platform's Tick polls GLFW first, then the ImGui host feeds io and
	// builds the UI (same frame).
	WaitFor<ITick, Platform::FPlatform, ITick>();
}

FRender::~FRender() = default;

void FRender::PreInitialize(FEngineBase&)
{
}

void FRender::Initialize(FEngineBase& Engine)
{
	(void)Engine;
	GRender = this;

	// Create the render server (RHI) with the native window from the Platform.
	Platform::FPlatform* P = Platform::GetPlatform();
	if (P != nullptr && P->GetNativeWindow() != nullptr)
	{
		RHI = std::make_unique<FRHI>();
		if (!RHI->Initialize(P->GetNativeWindow(), P->GetWindowWidth(), P->GetWindowHeight()))
		{
			MAHO_LOG_CORE_ERROR("FRender::Initialize: RHI initialization failed");
			RHI.reset();
		}
	}

	// RDG resource pool (off-screen textures/buffers, cross-frame reuse).
	ResourcePool = std::make_unique<FRenderResourcePool>(RHI.get());

	// ImGui host (owned like the RHI; NewFrame driven in Tick). Init only when
	// there is a real RHI + window (the RHI bridge needs the raw handles).
	if (RHI != nullptr && P != nullptr)
	{
		ImGui = std::make_unique<FImGuiSystem>();
		ImGui->Initialize(RHI.get(), P);
	}

	// Async shader compiler (dedicated compile thread).
	ShaderCompiler = std::make_unique<FShaderCompilerServer>();
	ShaderCompiler->Initialize();

	// Persistent render graph: Flush at frame start, Execute at frame end.
	RenderGraph = std::make_unique<FLayerTaskGraph<FRenderStages, FRender>>(Pool, *this);

	// Install the global scene feature + the triangle render feature into OUR
	// layer collection (not the host engine's) so the render graph drives them.
	// All are project plugins (Scene.dll / DrawTriangleFeature.dll / Frame.dll);
	// Frame drives the swapchain begin/end as a scheduled stage and must load last
	// (its IPresent depends on the other features' IEndRender).
	Install<Scene::FScene>();
	Install<FDrawTriangleFeature>();
	Install<FFrame>();
	Install<FImGuiRenderFeature>();
}

void FRender::PostInitialize(FEngineBase&)
{
}

void FRender::PreShutdown(FEngineBase&)
{
}

void FRender::Shutdown(FEngineBase&)
{
	// Clean-exit guarantee: drain EVERY async worker BEFORE tearing anything
	// down. The engine's shutdown graph runs the IShutdown stages concurrently,
	// so this layer must be quiescent before we touch shared state -- the render
	// feature graph pool first (a task still in flight holds the graph pointer),
	// then the two threaded servers (shader-compile thread + RHI render-server
	GRender = nullptr;
	// thread). All of them drain their queues and join here.
	//
	// The leftover render tasks drained below (e.g. the last frame's EndFrame)
	// may still present; the swapchain surface is still alive here because
	// FPlatform's Shutdown is ordered AFTER ours (it owns the window/surface the
	// RHI was created from). The RHI stays a stateless task processor -- it
	// never refuses work, it just runs what it is given.
	if (RenderGraph)
	{
		RenderGraph->Flush();   // render feature graph pool (quiescence barrier)
	}
	if (ShaderCompiler)
	{
		ShaderCompiler->Shutdown();   // compile thread: drain pending compiles, stop + join
	}
	if (RHI)
	{
		RHI->Shutdown();   // render-server thread: drain + join (device teardown stays in ShutdownRHI)
	}
	RenderGraph.reset();

	// All render work for the last frame was submitted (the graph drives the
	// features synchronously); the GPU may still be executing it. Wait here so
	// every resource destroyed below is released only after the device is idle.
	if (RHI)
	{
		RHI->WaitIdle();

		// The last frame's feature command lists are only recycled at the next
		// frame's IFrameBegin -- which never comes after the loop breaks. Destroy
		// them while the device is still alive (their VkCommandPools must not be
		// outstanding when the device is destroyed).
		ReleaseFrameLists();

		// Destroy the render feature instances explicitly BEFORE the RHI goes
		// away: their destructors free Vulkan objects (pipelines / shader
		// modules) that must be released while the device exists. The collector
		// would otherwise destroy them only when this FRender dies -- after the
		// RHI.
		Features.clear();
	}

	// The compile server thread was already joined above; just release the server.
	ShaderCompiler.reset();

	// Shut the ImGui host down after the render features (the ImGuiRender feature
	// was using imgui's Vulkan objects) and before the RHI device goes away.
	if (ImGui)
	{
		ImGui->Shutdown();
		ImGui.reset();
	}

	// Release pooled resources before the RHI device goes away.
	if (ResourcePool)
	{
		ResourcePool->Shutdown();
		ResourcePool.reset();
	}

	if (RHI)
	{
		RHI->ShutdownRHI();   // re-joining the server thread is a no-op; tears down the device
		RHI.reset();
	}
}

void FRender::PostShutdown(FEngineBase&)
{
}

void FRender::BeginFrame(FEngineBase&)
{
	// Frame begin/end are driven by the frame render feature (IFrameBegin/IFrameEnd)
	// inside the render graph -- this host stage is intentionally empty.
}

void FRender::ReleaseFrameLists()
{
	// The previous frame's feature command lists were submitted in their IEndRender
	// stages; after the frame feature's IFrameBegin waited the swapchain fence, they
	// are no longer executing -- destroy them now.
	std::vector<FRHICommandList*> Lists;
	{
		std::lock_guard<std::mutex> Lock(RenderListsMutex);
		Lists.swap(PendingRenderLists);
	}
	for (FRHICommandList* List : Lists)
	{
		if (RHI)
		{
			RHI->DestroyCommandList(List);
		}
	}
}

void FRender::BeginResourcePool()
{
	if (ResourcePool)
	{
		ResourcePool->BeginFrame();
	}
}

void FRender::Tick(FEngineBase&)
{
	if (!RenderGraph)
	{
		return;
	}

	// Frame-start barrier: wait the PREVIOUS frame's render-graph tasks before
	// rebuilding the graph. The render CPU work runs during the interval and is
	// collected here, so Init never races with in-flight Render() calls.
	RenderGraph->Flush();

	// Rebuild + drive the render feature graph. All frame work (swapchain begin,
	// feature acquire/record/submit, present, swapchain end) is a scheduled stage --
	// FRender only schedules; the frame feature + per-feature deps order it all.
	FlushPendingUpdatePipelines<IFrameBegin, IBeginRender, IRender, IEndRender, IPresent, IFrameEnd>();
	RenderGraph->Init(Select<IFrameBegin, IBeginRender, IRender, IEndRender, IPresent, IFrameEnd>());
	if (!RenderGraph->Compile())
	{
		ReportFatal("FRender::Tick: render pipeline Compile failed");
	}

	// Build the ImGui frame (input + NewFrame + UI + Render) BEFORE the render
	// graph executes -- the ImGuiRender feature records this same frame's draw
	// data, and it is ordered after FPlatform::Tick (input polled first).
	if (ImGui)
	{
		ImGui->NewFrame();
	}

	RenderGraph->Execute();
	// No trailing Flush: the render graph pipelines across frames -- the next
	// Tick's leading Flush (above) waits this frame's tasks. At shutdown the
	// leftover tasks are drained by FRender::Shutdown (render-pool Flush at its
	// start) and FLog's Shutdown is ordered after FRender's, so any teardown
	// logging lands in a live logger.
}

void FRender::EndFrame(FEngineBase&)
{
	// Frame begin/end are driven by the frame render feature (IFrameBegin/IFrameEnd)
	// inside the render graph -- this host stage is intentionally empty.
}

void FRender::RequestExit(FEngineBase&)
{
}

FRDGTextureRef FRender::CreateTexture(const FRHITextureDesc& Desc, bool bTransient)
{
	return ResourcePool ? ResourcePool->CreateTexture(Desc, bTransient) : FRDGTextureRef{};
}

FRDGBufferRef FRender::CreateBuffer(const FRHIBufferDesc& Desc, bool bTransient)
{
	return ResourcePool ? ResourcePool->CreateBuffer(Desc, bTransient) : FRDGBufferRef{};
}

void FRender::ReleaseTexture(FRDGTextureRef& Ref)
{
	if (ResourcePool)
	{
		ResourcePool->ReleaseTexture(Ref);
	}
}

void FRender::ReleaseBuffer(FRDGBufferRef& Ref)
{
	if (ResourcePool)
	{
		ResourcePool->ReleaseBuffer(Ref);
	}
}

FRHICommandList* FRender::AcquireRenderList()
{
	if (RHI == nullptr)
	{
		return nullptr;
	}
	FRHICommandList* List = RHI->CreateCommandList(ERHICommandListType::Graphics);
	if (List != nullptr)
	{
		std::lock_guard<std::mutex> Lock(RenderListsMutex);
		PendingRenderLists.push_back(List);
	}
	return List;
}

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_RENDER_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FRender::CreateLayer();
}
