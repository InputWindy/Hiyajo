#include "Render.h"

#include <Platform.h>

// Unity build: fold the shader compiler TU in (the codegen target only compiles
// Render.cpp; the RHI plugin uses the same pattern).
#include "ShaderCompiler.cpp"

namespace Maho
{

FRender::FRender()
{
	// My Initialize depends on Platform's PostInitialize: the window must be
	// created (and GPlatform published) before the RHI reads the native handle.
	AddDependency(std::type_index(typeid(IInit)), "FPlatform", std::type_index(typeid(IPostInit)));
}

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
		if (!RHI->Initialize(P->GetNativeWindow(), 1280, 720))
		{
			MAHO_LOG_CORE_ERROR("FRender::Initialize: RHI initialization failed");
			RHI.reset();
		}
	}

	// Async shader compiler (dedicated compile thread).
	ShaderCompiler = std::make_unique<FShaderCompilerServer>();
	ShaderCompiler->Initialize();

	// Persistent render graph: Flush at frame start, Execute at frame end.
	RenderGraph = std::make_unique<FLayerTaskGraph<FRenderStages, FRender>>(Pool, *this);

	// Install the clear-color render feature into OUR layer collection (not the
	// host engine's) so the render graph drives it.
	Install("ClearFeature.dll");

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
	// The RHI frame primitives (BeginFrame/Clear/EndFrame) are serial and must
	// complete before EndFrame presents, so wait for the render features now.
	RenderGraph->Flush();
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

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_RENDER_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FRender::CreateLayer();
}
