#pragma once

#include "RenderApi.h"
#include <Maho.h>
#include <Engine/Layer.h>
#include <Engine/LayerCollector.h>
#include <Engine/LayerTaskGraph.h>
#include <Engine/Engine.h>
#include <RHI/RHIServer.h>
#include "ShaderCompiler.h"

#include <memory>

namespace Maho
{

class FRender;

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

MAHO_DECLARE_STAGE_DISPATCH(FRender, IBeginRender, IBeginRender, BeginRender)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IRender,      IRender,      Render)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IEndRender,   IEndRender,   EndRender)

/**
 * Render subsystem - a layer in the host engine (mounted as IInit/ITick/...),
 * plus its own layer collector for render features (implementing IBeginRender/
 * IRender/IEndRender), plus a dedicated render thread (FThreadedServer). The
 * host engine only sees FRender as one layer; render features are installed and
 * scheduled entirely inside FRender.
 */
class MAHO_RENDER_API FRender
	: public FLayer<IPreInit, IInit, IPostInit, IBeginFrame, ITick, IEndFrame, IExit, IPreShutdown, IShutdown, IPostShutdown>
	, public FLayerCollector<FRender>
	, public FThreadedServer
{
MAHO_DECLARE_LAYER(FRender, "Render.dll");

	FRender();

	/** The RHI command surface (render features reach it through this). */
	IRHI* GetRHI() const { return RHI.get(); }

	/** The async shader compiler (render features submit compile requests). */
	FShaderCompilerServer* GetShaderCompiler() const { return ShaderCompiler.get(); }

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

	// Persistent render graph - Flush() at frame start waits the previous frame's
	// async tasks, Execute() at frame end submits the next frame's, so the render
	// thread pipelines work across frames.
	using FRenderStages = TTypeList<IBeginRender, IRender, IEndRender>;
	std::unique_ptr<FLayerTaskGraph<FRenderStages, FRender>> RenderGraph;
};

} // namespace Maho
