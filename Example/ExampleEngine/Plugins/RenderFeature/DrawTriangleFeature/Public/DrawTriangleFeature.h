#pragma once

#include "DrawTriangleFeatureApi.h"
#include <Maho.h>
#include <Engine/Layer.h>
#include <Render.h>
#include <RHI/RHIServer.h>

namespace Maho
{

// DrawTriangleFeature - a render feature that compiles a fullscreen triangle
// shader and draws it into the swapchain backbuffer each frame. Mounts only
// the IRender stage.
class FDrawTriangleFeature : public FLayer<IRender>
{
MAHO_DECLARE_LAYER(FDrawTriangleFeature, "DrawTriangleFeature.dll");

private:
	void Render(FRender& R) override;

	// Lazily-built triangle resources.
	FRHIShaderModule* VS = nullptr;
	FRHIShaderModule* FS = nullptr;
	FRHIPipelineLayout* Layout = nullptr;
	FRHIGraphicsPipeline* Pipeline = nullptr;
	bool bBuilt = false;
};

} // namespace Maho
