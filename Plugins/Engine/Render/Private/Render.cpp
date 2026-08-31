#include "Render.h"

#include <Platform.h>
#include "RenderResourcePool.h"

// Unity build: fold the shader compiler + resource pool TUs in (the codegen
// target only compiles Render.cpp; the RHI plugin uses the same pattern).
#include "ShaderCompiler.cpp"
#include "RenderResourcePool.cpp"

namespace Maho
{

FRender::FRender()
{
	// My Initialize depends on Platform's PostInitialize: the window must be
	// created (and GPlatform published) before the RHI reads the native handle.
	AddDependency(std::type_index(typeid(IInit)), "FPlatform", std::type_index(typeid(IPostInit)));
}

FRender::~FRender() = default;

void FRender::PreInitialize(FEngineBase&)
{
}

void FRender::Initialize(FEngineBase& Engine)
{
	(void)Engine;

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
	// Both are project plugins (Scene.dll / DrawTriangleFeature.dll).
	Install("Scene.dll");
	Install("DrawTriangleFeature.dll");

	FThreadedServer::Initialize();
}

void FRender::PostInitialize(FEngineBase&)
{
}

void FRender::PreShutdown(FEngineBase&)
{
}

void FRender::Shutdown(FEngineBase&)
{
	// Wait pending render tasks, then release the render graph (before the pool
	// in FLayerCollector is destroyed).
	RenderGraph.reset();

	// Stop the shader compile thread first.
	if (ShaderCompiler)
	{
		ShaderCompiler->FlushCompiles();
		ShaderCompiler.reset();
	}

	// Release pooled resources before the RHI device goes away.
	if (ResourcePool)
	{
		ResourcePool->Shutdown();
		ResourcePool.reset();
	}

	if (RHI)
	{
		RHI->ShutdownRHI();
		RHI.reset();
	}
	FThreadedServer::Shutdown();
}

void FRender::PostShutdown(FEngineBase&)
{
}

void FRender::BeginFrame(FEngineBase&)
{
	if (RHI)
	{
		RHI->BeginFrame();
	}
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

	// Apply pending installs/uninstalls of render features, then drive the
	// render feature graph (IBeginRender -> IRender -> IEndRender).
	FlushPendingUpdatePipelines<IBeginRender, IRender, IEndRender>();

	RenderGraph->Init(Select<IBeginRender, IRender, IEndRender>());
	if (!RenderGraph->Compile())
	{
		ReportFatal("FRender::Tick: render pipeline Compile failed");
	}
	RenderGraph->Execute();
	// The RHI frame primitives (BeginFrame/Present/EndFrame) are serial and must
	// complete before EndFrame submits, so wait for the render features now.
	RenderGraph->Flush();

	// IPresent runs AFTER the graph drains: the present point records into the
	// SAME shared frame command buffer, so it must never overlap graph nodes.
	// Only the IPresent capability is dispatched - FRender has no knowledge of
	// which concrete feature (Scene, UI) provides the present surface.
	auto Presenters = Select<IPresent>();
	for (FLayerBase* L : Presenters.Data)
	{
		Invoke<IPresent, FRender>(L, *this);
	}
}

void FRender::EndFrame(FEngineBase&)
{
	if (RHI)
	{
		RHI->EndFrame();
	}
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

void FRender::Present(FRDGTextureRef& Src)
{
	if (RHI && Src.IsValid())
	{
		RHI->PresentTexture(Src.GetRHI());
	}
}

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_RENDER_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FRender::CreateLayer();
}
