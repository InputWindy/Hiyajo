#pragma once

#include "DrawTriangleFeatureApi.h"
#include <Maho.h>
#include <Engine/Layer.h>
#include <Render.h>
#include <RHI/RHIServer.h>

namespace Maho
{

// DrawTriangleFeature - a render feature that compiles a fullscreen triangle
// shader and draws it into the shared FScene::SceneColor target each frame
// (dynamic rendering, Load over the scene clear). Acquires a command list in
// IBeginRender, records the draw in IRender, submits it in IEndRender.
class FDrawTriangleFeature : public FLayer<IBeginRender, IRender, IEndRender>
{
MAHO_DECLARE_LAYER(FDrawTriangleFeature, "DrawTriangleFeature.dll");

	FDrawTriangleFeature();
	~FDrawTriangleFeature() override;

private:
	void BeginRender(FRender& R) override;
	void Render(FRender& R) override;
	void EndRender(FRender& R) override;

	// Lazily-built triangle resources.
	FRHIShaderModule* VS = nullptr;
	FRHIShaderModule* FS = nullptr;
	FRHIPipelineLayout* Layout = nullptr;
	FRHIGraphicsPipeline* Pipeline = nullptr;
	IRHI* OwnerRHI = nullptr;   // the RHI the pipeline was created on; the dtor releases through it
	bool bBuilt = false;

	FRHICommandList* RenderList = nullptr;   // acquired in BeginRender, recorded in Render, submitted in EndRender
};

} // namespace Maho
