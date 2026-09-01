#pragma once

#include "RenderApi.h"
#include <Maho.h>
#include <Engine/Layer.h>
#include <Engine/LayerCollector.h>
#include <Engine/LayerTaskGraph.h>
#include <Engine/Engine.h>
#include <RHI/RHIServer.h>
#include "RDG.h"
#include "ShaderCompiler.h"

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace Maho
{

class FRender;
class FRenderResourcePool;
class FImGuiSystem;

/** Global render instance accessor (cross-DLL via function, no bare variable
 *  export) -- set when the Render layer initializes. Other layers (e.g. the
 *  ImGui layer) use it to reach the RHI. */
MAHO_RENDER_API FRender* GetRender();

class MAHO_RENDER_API IInitViews
{
public:
	virtual ~IInitViews() = default;
	virtual void InitViews(FRender&) = 0;
};

class MAHO_RENDER_API IBeginRender
{
public:
	virtual ~IBeginRender() = default;
	virtual void BeginRender(FRender&) = 0;
};

class MAHO_RENDER_API IRender
{
public:
	virtual ~IRender() = default;
	virtual void Render(FRender&) = 0;
};

class MAHO_RENDER_API IEndRender
{
public:
	virtual ~IEndRender() = default;
	virtual void EndRender(FRender&) = 0;
};

class MAHO_RENDER_API IPostProcess
{
public:
	virtual ~IPostProcess() = default;
	virtual void PostProcess(FRender&) = 0;
};

class MAHO_RENDER_API IRenderUI
{
public:
	virtual ~IRenderUI() = default;
	virtual void RenderUI(FRender&) = 0;
};

class MAHO_RENDER_API IPresent
{
public:
	virtual ~IPresent() = default;
	virtual void Present(FRender&) = 0;
};

MAHO_DECLARE_STAGE_DISPATCH(FRender, IInitViews,   IInitViews,   InitViews)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IBeginRender, IBeginRender, BeginRender)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IRender,      IRender,      Render)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IEndRender,   IEndRender,   EndRender)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IPostProcess, IPostProcess, PostProcess)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IRenderUI,    IRenderUI,    RenderUI)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IPresent,     IPresent,     Present)

/**
 * Render subsystem - a layer in the host engine (mounted as IInit/ITick/...),
 * plus its own layer collector for render features (implementing IBeginRender/
 * IRender/IEndRender/IPresent). The dedicated threads live INSIDE the pieces it
 * owns: the RHI (FRHI) is a render server (FThreadedServer) and the shader
 * compiler (FShaderCompilerServer) is its own FThreadedServer - FRender itself
 * is NOT a server. The host engine only sees FRender as one layer; render
 * features are installed and scheduled entirely inside FRender.
 *
 * FRender is also the RDG resource pool: features create off-screen resources
 * through CreateTexture/CreateBuffer and hand back FRDG*Ref handles; the native
 * FRHITexture/FRHIBuffer live in the pool (cross-frame reuse). The swapchain
 * backbuffer is only ever touched by the frame feature (via RHI->PresentTexture
 * on GetRHI()).
 */
class MAHO_RENDER_API FRender
	: public FLayer<IPreInit, IInit, IPostInit, IBeginFrame, ITick, IEndFrame, IExit, IPreShutdown, IShutdown, IPostShutdown>
	, public FLayerCollector<FRender>
{
MAHO_DECLARE_LAYER(FRender, "Render.dll");

	FRender();
	~FRender() override;

public:
	/** The RHI command surface (render features reach it through this). */
	IRHI* GetRHI() const { return RHI.get(); }

	/** The async shader compiler (render features submit compile requests). */
	FShaderCompilerServer* GetShaderCompiler() const { return ShaderCompiler.get(); }

	/** The ImGui host (owned like the RHI; driven by this layer's stages). */
	FImGuiSystem* GetImGui() const { return ImGui.get(); }

	// -- RDG resource pool (off-screen resources) --
	[[nodiscard]] FRDGTextureRef CreateTexture(const FRHITextureDesc& Desc, bool bTransient = false);
	[[nodiscard]] FRDGBufferRef CreateBuffer(const FRHIBufferDesc& Desc, bool bTransient = false);
	void ReleaseTexture(FRDGTextureRef& Ref);
	void ReleaseBuffer(FRDGBufferRef& Ref);

	/** Destroy the previous frame's feature command lists -- call after the frame
	 *  host BeginFrame has waited the swapchain fence (their submits are done). */
	void ReleaseFrameLists();

	/** Advance the RDG resource pool (expire transients) -- called by the frame
	 *  host BeginFrame. */
	void BeginResourcePool();

	/** Record a render-feature pass into ITS OWN command list. The feature owns the
	 *  list lifecycle across its stages: acquire in IBeginRender, record in IRender,
	 *  submit in IEndRender. The list is tracked here for deferred destruction
	 *  (it may still be executing on the GPU until the next frame's fence wait). */
	[[nodiscard]] FRHICommandList* AcquireRenderList();

protected:
	// -- host engine stages (FEngineBase context) --
	void PreInitialize(FEngineBase&) override;
	void Initialize(FEngineBase& Engine) override;
	void PostInitialize(FEngineBase&) override;
	void PreShutdown(FEngineBase&) override;
	void Shutdown(FEngineBase& Engine) override;
	void PostShutdown(FEngineBase&) override;
	void BeginFrame(FEngineBase& Engine) override;
	void Tick(FEngineBase& Engine) override;
	void EndFrame(FEngineBase& Engine) override;
	void RequestExit(FEngineBase& Engine) override;

private:
	std::unique_ptr<FRHI> RHI;   // the render server (not a scheduled layer)
	std::unique_ptr<FShaderCompilerServer> ShaderCompiler;   // async GLSL -> SPIR-V
	std::unique_ptr<FRenderResourcePool> ResourcePool;   // RDG resource pool
	std::unique_ptr<FImGuiSystem> ImGui;   // the ImGui host (context + imgui_impl_vulkan)

	// Render graph stages. The swapchain frame lifecycle (acquire / end + present)
	// lives on the host FRender::BeginFrame/EndFrame (engine stages); the graph
	// runs the draw passes + the present blit, and FRender::EndFrame drains it
	// (Flush) before RHI->EndFrame so the present waits every submit.
	// TaskGraph orders everything. FRender itself does no frame work.
	using FRenderStages = TTypeList<IInitViews, IBeginRender, IRender, IEndRender, IPostProcess, IRenderUI, IPresent>;
	std::unique_ptr<FLayerTaskGraph<FRenderStages, FRender>> RenderGraph;

	std::vector<FRHICommandList*> PendingRenderLists;   // lists acquired this frame; destroyed at the next BeginFrame
	std::mutex RenderListsMutex;                        // guards PendingRenderLists (features acquire on pool workers)
};

} // namespace Maho
