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

#include <memory>

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

MAHO_DECLARE_STAGE_DISPATCH(FRender, IBeginRender, IBeginRender, BeginRender)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IRender,      IRender,      Render)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IEndRender,   IEndRender,   EndRender)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IPresent,     IPresent,     Present)

/**
 * Render subsystem - a layer in the host engine (mounted as IInit/ITick/...),
 * plus its own layer collector for render features (implementing IBeginRender/
 * IRender/IEndRender/IPresent), plus a dedicated render thread (FThreadedServer).
 * The host engine only sees FRender as one layer; render features are installed
 * and scheduled entirely inside FRender.
 *
 * FRender is also the RDG resource pool: features create off-screen resources
 * through CreateTexture/CreateBuffer and hand back FRDG*Ref handles; the native
 * FRHITexture/FRHIBuffer live in the pool (cross-frame reuse). Present(Ref) is
 * the ONLY touch point with the swapchain backbuffer.
 */
class MAHO_RENDER_API FRender
	: public FLayer<IPreInit, IInit, IPostInit, IBeginFrame, ITick, IEndFrame, IExit, IPreShutdown, IShutdown, IPostShutdown>
	, public FLayerCollector<FRender>
	, public FThreadedServer
{
MAHO_DECLARE_LAYER(FRender, "Render.dll");

	FRender();
	~FRender() override;

public:
	/** The RHI command surface (render features reach it through this). */
	IRHI* GetRHI() const { return RHI.get(); }

	/** The async shader compiler (render features submit compile requests). */
	FShaderCompilerServer* GetShaderCompiler() const { return ShaderCompiler.get(); }

	/** Borrow the frame command buffer (already begun; ended/submitted by FRender). */
	FRHICommandList* GetFrameCommandList() const { return RHI ? RHI->GetFrameCommandList() : nullptr; }

	[[nodiscard]] std::uint32_t GetFramebufferWidth() const { return RHI ? RHI->GetFramebufferWidth() : 0; }
	[[nodiscard]] std::uint32_t GetFramebufferHeight() const { return RHI ? RHI->GetFramebufferHeight() : 0; }
	[[nodiscard]] ERHIFormat GetSwapchainFormat() const { return RHI ? RHI->GetSwapchainFormat() : ERHIFormat::Unknown; }

	// -- RDG resource pool (off-screen resources) --
	[[nodiscard]] FRDGTextureRef CreateTexture(const FRHITextureDesc& Desc, bool bTransient = false);
	[[nodiscard]] FRDGBufferRef CreateBuffer(const FRHIBufferDesc& Desc, bool bTransient = false);
	void ReleaseTexture(FRDGTextureRef& Ref);
	void ReleaseBuffer(FRDGBufferRef& Ref);

	/** Copy an off-screen color texture to the swapchain backbuffer (IPresent stage). */
	void Present(FRDGTextureRef& Src);

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
	using FRenderStages = TTypeList<IBeginRender, IRender, IEndRender>;
	std::unique_ptr<FLayerTaskGraph<FRenderStages, FRender>> RenderGraph;
};

} // namespace Maho
