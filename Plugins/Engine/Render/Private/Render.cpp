#include "Render.h"

#include <DrawTriangleFeature.h>
#include <Frame.h>
#include <UIFeature.h>
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
	MyStage<IInit>().IsWaiting<Platform::FPlatform>().ForStage<IPostInit>();
	MyStage<IInit>().IsWaiting<FLog>().ForStage<IInit>();

	// Shutdown: my teardown drains render tasks that may log, and I hold the RHI
	// surface created from Platform's window -- so Log and Platform must run
	// their Shutdown AFTER mine. Declared here (I know them), not by them.
	MyStage<IShutdown>().IsBlocking<FLog>().OnStage<IShutdown>();
	MyStage<IShutdown>().IsBlocking<Platform::FPlatform>().OnStage<IShutdown>();

	// Input: Platform's Tick polls GLFW first, then the ImGui host feeds io and
	// builds the UI (same frame).
	MyStage<ITick>().IsWaiting<Platform::FPlatform>().ForStage<ITick>();

	// UI data is produced by FUI::ITick; my render graph drives the UI feature's
	// RenderUI in ITS Tick. Render depends on UI (unidirectional), so the wait
	// is declared HERE, by the consumer (me): my Tick runs after FUI::Tick, so
	// ProcessUIData has already filled UIData before the UI feature draws it.
	MyStage<ITick>().IsWaiting<FUI>().ForStage<ITick>();
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
	Install<FUIFeature>();
}

void FRender::PostInitialize(FEngineBase&)
{
}

void FRender::WaitShaderCompiles()
{
	if (ShaderCompiler)
	{
		// The shader server is a quiescence barrier: FlushCompiles waits until every
		// CompileAsync submitted so far has completed. Calling this from a handle's
		// Wait() is the "sync before use" point -- after it returns, the per-T state
		// holds the compiled bytecode (and bReady).
		ShaderCompiler->FlushCompiles();
	}
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

		// The last frame's feature command lists + pooled resources are destroyed
		// below by ResourcePool->Shutdown() while the device is still alive (their
		// VkCommandPools / VkBuffers must not be outstanding when the device dies).

		// Destroy the render feature instances explicitly BEFORE the RHI goes
		// away: their destructors free Vulkan objects (pipelines / shader
		// modules) that must be released while the device exists. The collector
		// would otherwise destroy them only when this FRender dies -- after the
		// RHI.
		Features.clear();
	}

	// The compile server thread was already joined above; just release the server.
	ShaderCompiler.reset();

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
	// Swapchain frame lifecycle lives on the host (engine) stages, not the render
	// graph: RHI->BeginFrame waits the previous fence, acquires the swapchain
	// image and begins the frame buffer; then the previous frame's feature
	// command lists are recycled (their submits are done) and the resource pool
	// advances -- all before the render graph runs in Tick.
	if (IRHI* RHIp = RHI.get())
	{
		RHIp->BeginFrame();
	}
	BeginResourcePool();
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
	FlushPendingUpdatePipelines<IInitViews, IBeginRender, IRender, IEndRender, IPostProcess, IRenderUI, IPresent>();
	RenderGraph->Init(Select<IInitViews, IBeginRender, IRender, IEndRender, IPostProcess, IRenderUI, IPresent>());
	if (!RenderGraph->Compile())
	{
		ReportFatal("FRender::Tick: render pipeline Compile failed");
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
	// RHI->EndFrame (end + submit the frame buffer, present the swapchain) must
	// run after every feature submit, so drain the async render-graph tasks first.
	// This serializes the present behind this frame's draws (the graph is no
	// longer cross-frame pipelined across Tick -- a later auto-barrier pass can
	// restore the overlap).
	if (RenderGraph)
	{
		RenderGraph->Flush();
	}
	if (IRHI* RHIp = RHI.get())
	{
		RHIp->EndFrame();
	}
}

void FRender::RequestExit(FEngineBase&)
{
}

FRDGTextureRef FRender::CreateTexture(const FRHITextureDesc& Desc, ERDGResourceLifetime Lifetime)
{
	return ResourcePool ? ResourcePool->CreateTexture(Desc, Lifetime) : FRDGTextureRef{};
}

FRDGBufferRef FRender::CreateBuffer(const FRHIBufferDesc& Desc, ERDGResourceLifetime Lifetime)
{
	return ResourcePool ? ResourcePool->CreateBuffer(Desc, Lifetime) : FRDGBufferRef{};
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

void FRender::AddPass(ERHICommandListType PassType, std::function<void(FRHICommandList&)> PassFn)
{
	SubmitPass(PassType, std::move(PassFn));
}

void FRender::SubmitPass(ERHICommandListType PassType, std::function<void(FRHICommandList&)> Record)
{
	FRHICommandList* List = ResourcePool ? ResourcePool->AcquireRenderList() : nullptr;
	if (List == nullptr)
	{
		return;
	}
	List->Begin();
	Record(*List);
	List->End();
	if (IRHI* P = RHI.get())
	{
		P->Submit(List, PassType);
	}
}

FRHIShaderModule* FRender::GetOrCreateShaderModule(const FRHIShaderModuleDesc& Desc)
{
	return ResourcePool ? ResourcePool->GetOrCreateShaderModule(Desc) : nullptr;
}

FRHIPipelineLayout* FRender::GetOrCreatePipelineLayout(const FRHIPipelineLayoutDesc& Desc)
{
	return ResourcePool ? ResourcePool->GetOrCreatePipelineLayout(Desc) : nullptr;
}

FRHIDescriptorSetLayout* FRender::GetOrCreateDescriptorSetLayout(const FRHIDescriptorSetLayoutDesc& Desc)
{
	return ResourcePool ? ResourcePool->GetOrCreateDescriptorSetLayout(Desc) : nullptr;
}

FRHIGraphicsPipeline* FRender::GetOrCreateGraphicsPipeline(const FRHIGraphicsPipelineDesc& Desc)
{
	return ResourcePool ? ResourcePool->GetOrCreateGraphicsPipeline(Desc) : nullptr;
}

FRHISampler* FRender::CreateSampler(const FRHISamplerDesc& Desc)
{
	return ResourcePool ? ResourcePool->CreateSampler(Desc) : nullptr;
}

FRHIDescriptorSet* FRender::CreateDescriptorSet(FRHIDescriptorSetLayout* Layout, const FRHIDescriptorSetLayoutDesc& LayoutDesc)
{
	return ResourcePool ? ResourcePool->CreateDescriptorSet(Layout, LayoutDesc) : nullptr;
}

std::uint32_t FRender::GetCanvasWidth() const
{
	return RHI ? RHI->GetFramebufferWidth() : 0;
}

std::uint32_t FRender::GetCanvasHeight() const
{
	return RHI ? RHI->GetFramebufferHeight() : 0;
}

ERHIFormat FRender::GetSwapchainFormat() const
{
	return RHI ? RHI->GetSwapchainFormat() : ERHIFormat::Unknown;
}

void FRender::PresentTexture(const FRDGTextureRef& Texture)
{
	if (RHI && ResourcePool)
	{
		RHI->PresentTexture(ResourcePool->GetTexture(Texture));
	}
}

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_RENDER_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FRender::CreateLayer();
}
