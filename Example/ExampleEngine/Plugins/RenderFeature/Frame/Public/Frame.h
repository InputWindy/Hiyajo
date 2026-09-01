#pragma once

#include "FrameApi.h"
#include <Maho.h>
#include <Engine/Layer.h>
#include <Render.h>
#include <Scene.h>

namespace Maho
{

/**
 * Frame render feature - drives the swapchain frame lifecycle as a scheduled
 * render stage, so FRender stays a pure scheduler (no host-side frame work):
 *   IFrameBegin : acquire the swapchain image + begin the frame buffer + recycle
 *                 the previous frame's command lists
 *   IPresent    : blit the scene color to the swapchain backbuffer
 *   IFrameEnd   : submit the frame buffer + present
 * The pipeline self-progression orders IFrameBegin -> IPresent -> IFrameEnd; its
 * IPresent depends on every render feature's IEndRender, so the present is ordered
 * after all draws.
 */
class MAHO_FRAME_API FFrame : public FLayer<IFrameBegin, IPresent, IFrameEnd>
{
MAHO_DECLARE_LAYER(FFrame, "Frame.dll");

	FFrame();

public:
	void BeginFrame(FRender& R) override;
	void Present(FRender& R) override;
	void EndFrame(FRender& R) override;
};

} // namespace Maho
