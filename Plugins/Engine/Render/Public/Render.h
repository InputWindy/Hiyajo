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

class MAHO_RENDER_API IPresent
{
public:
	virtual ~IPresent() = default;
	virtual void Present(FRender&) = 0;
};

/** Frame lifecycle stages -- implemented by a dedicated "frame" render feature so
 *  the host FRender is a pure scheduler and the swapchain begin/end is part of the
 *  render graph (scheduled like any stage). */
class MAHO_RENDER_API IFrameBegin
{
public:
	virtual ~IFrameBegin() = default;
	virtual void BeginFrame(FRender&) = 0;
};

class MAHO_RENDER_API IFrameEnd
{
public:
	virtual ~IFrameEnd() = default;
	virtual void EndFrame(FRender&) = 0;
};

MAHO_DECLARE_STAGE_DISPATCH(FRender, IBeginRender, IBeginRender, BeginRender)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IRender,      IRender,      Render)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IEndRender,   IEndRender,   EndRender)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IPresent,     IPresent,     Present)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IFrameBegin,  IFrameBegin,  BeginFrame)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IFrameEnd,    IFrameEnd,    EndFrame)

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

	// -- RDG resource pool (off-screen resources) --
	[[nodiscard]] FRDGTextureRef CreateTexture(const FRHITextureDesc& Desc, bool bTransient = false);
	[[nodiscard]] FRDGBufferRef CreateBuffer(const FRHIBufferDesc& Desc, bool bTransient = false);
	void ReleaseTexture(FRDGTextureRef& Ref);
	void ReleaseBuffer(FRDGBufferRef& Ref);

	/** Destroy the previous frame's feature command lists -- call after the frame
	 *  feature's IFrameBegin has waited the swapchain fence (their submits are done). */
	void ReleaseFrameLists();

	/** Advance the RDG resource pool (expire transients) -- called by the frame
	 *  feature's IFrameBegin. */
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

	// Persistent render graph - Flush() at frame start waits the previous frame's
	// async tasks, Execute() at frame end submits the next frame's, so the render
	// thread pipelines work across frames. IPresent is NOT part of the graph: it
	// is driven by FRender::Tick after the graph flushes, so the shared frame
	// command buffer is never recorded concurrently.
	// Render graph stages. The frame feature (IFrameBegin/IFrameEnd) drives the
	// swapchain begin/end as a scheduled stage, and IPresent is scheduled too -- the
	// present feature declares its deps on the other features' last stage, so the
	// TaskGraph orders everything. FRender itself does no frame work.
	using FRenderStages = TTypeList<IFrameBegin, IBeginRender, IRender, IEndRender, IPresent, IFrameEnd>;
	std::unique_ptr<FLayerTaskGraph<FRenderStages, FRender>> RenderGraph;

	std::vector<FRHICommandList*> PendingRenderLists;   // lists acquired this frame; destroyed at the next BeginFrame
	std::mutex RenderListsMutex;                        // guards PendingRenderLists (features acquire on pool workers)
};

} // namespace Maho
