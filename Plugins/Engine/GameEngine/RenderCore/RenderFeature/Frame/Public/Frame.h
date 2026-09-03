#pragma once

#include "FrameApi.h"
#include <Maho.h>
#include <Engine/Layer.h>
#include <Render.h>
#include <Scene.h>

namespace Maho
{

/**
 * Frame render feature - owns the present blit as a scheduled render stage. The
 * swapchain frame lifecycle (acquire / end + present) lives on the host
 * FRender::BeginFrame/EndFrame (engine stages), not here:
 *   IPresent : blit the scene color to the swapchain backbuffer
 * IPresent depends on every render feature's last draw stage, so the present is
 * ordered after all draws.
 */
class MAHO_FRAME_API FFrame : public FLayer<IPresent>
{
MAHO_DECLARE_LAYER(FFrame, "Frame.dll");

	FFrame();

public:
	void Present(FRender& R) override;
};

} // namespace Maho
